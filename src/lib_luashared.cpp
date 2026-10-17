/*
** ILuaShared tail helpers for the Garry's Mod platform layer.
**
** The engine drives several ILuaShared slots through std::string references
** (LoadFile / GetCache / ScriptExists) and decides whether a gamemode exists
** by asking LoadFile for the script and checking that it returned a LuaFile.
** This translation unit builds those std::string-typed answers and hands back a
** LuaFile with the SHIPPING layout, so the engine sees the same bytes it
** expects.
**
** Script bytes come from the engine's file system first, because that is where
** the mounted search paths, the addon folders and the packed archives live.
** The interface is requested during Init and published through this module's
** export table, exactly like the reference build does, so third party modules
** that read the pointer see the same object the loader uses.
**
** Only the interface's base file system block is driven (Read, Write, Open,
** Close, Seek, Tell, Size, Flush, Precache, FileExists, ... , GetFileTime,
** ReadFile, WriteFile, UnzipFile).  That block sits one pointer slot into the
** object, has kept the same order in every Source generation, and is where the
** reference loader asks its own existence, timestamp and read questions.  The
** interface's own block grew and reordered methods between builds, so its
** entries are not used: directory enumeration and the search path bookkeeping
** run on the local walk, which resolves names against the roots the mounting
** slots recorded.  That keeps this module usable outside the engine (self
** tests, tooling) and keeps a name reachable even when the engine mounted its
** directory somewhere this module was not told about.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "GarrysMod/Lua/LuaShared.h"

/* The engine exports its file system pointer under the decorated name of this
** declaration, and modules that import it bind here.  Init fills it in from the
** factory answer; a host without a file system leaves it NULL. */
class IFileSystem;
#ifdef _WIN32
__declspec( dllexport ) IFileSystem *g_pFullFileSystem = NULL;
#endif

/* Implemented in lib_gmod.c: name resolution against the recorded roots. */
extern "C" int opglua_read_script_id( const char *name, const char *pathID, char **outData );
extern "C" int opglua_script_exists_id( const char *name, const char *pathID );
extern "C" int opglua_script_isdir_id( const char *name, const char *pathID );
extern "C" int opglua_find_scripts( const char *path, const char *pathID,
                                    void *ctx, void (*cb)( void *, const char *, int ) );

namespace {

/* ------------------------------------------------------------------------ */
/* Engine file system bridge.                                              */
/*                                                                          */
/* IFileSystem is two sub-objects: the app system head plus the file         */
/* system's own methods at offset zero, and the base file system block one   */
/* pointer slot in.  The base block answers the file questions; the own block */
/* answers the directory ones and carries the search path trio.              */
/*                                                                          */
/* Base block order (unchanged since the first Source release):             */
/*                                                                          */
/*   0 Read( out, size, handle )      8 Flush( handle )                     */
/*   1 Write( in, size, handle )      9 Precache( name, pathID )            */
/*   2 Open( name, options, pathID ) 10 FileExists( name, pathID )          */
/*   3 Close( handle )               11 IsFileWritable( name, pathID )      */
/*   4 Seek( handle, pos, where )    12 SetFileWritable( name, writable, ..) */
/*   5 Tell( handle )                13 GetFileTime( name, pathID )         */
/*   6 Size( handle )                14 ReadFile( name, pathID, buffer )    */
/*   7 Size( name, pathID )          15 WriteFile( name, pathID, buffer )   */
/*                                                                          */
/* Own block entries, each confirmed by where the reference loader and its   */
/* mounting code call them: AddSearchPath, RemoveSearchPaths,                */
/* MarkPathIDByRequestOnly, IsDirectory, FindFirst/FindNext/                 */
/* FindIsDirectory/FindClose.  The find handle is a four byte integer: the   */
/* entry that inspects it reads a dword and masks one bit out of it.          */

void *g_fs = NULL;          /* the interface itself */
void *g_fs_base = NULL;     /* its base block */
bool  g_fs_usable = true;   /* cleared once a call misbehaves */

enum BaseSlot {
  kBaseRead = 0,
  kBaseOpen = 2,
  kBaseClose = 3,
  kBaseSizeHandle = 6,
  kBaseFileExists = 10,
  kBaseGetFileTime = 13
};

enum OwnSlot {
  kOwnAddSearchPath = 11,
  kOwnRemoveSearchPaths = 14,
  kOwnMarkPathIDByRequestOnly = 15,
  kOwnIsDirectory = 22,
  kOwnFindNext = 32,
  kOwnFindIsDirectory = 33,
  kOwnFindClose = 34,
  kOwnFindFirstEx = 35
};

typedef bool         (*BaseFileExistsFn)( void *base, const char *name, const char *pathID );
typedef long long    (*BaseGetFileTimeFn)( void *base, const char *name, const char *pathID );
typedef void *       (*BaseOpenFn)( void *base, const char *name, const char *options,
                                    const char *pathID );
typedef int          (*BaseReadFn)( void *base, void *out, int size, void *handle );
typedef unsigned int (*BaseSizeFn)( void *base, void *handle );
typedef void         (*BaseCloseFn)( void *base, void *handle );

typedef void (*OwnAddSearchPathFn)( void *fs, const char *path, const char *pathID, int addType );
typedef void (*OwnMarkPathIDFn)( void *fs, const char *pathID, bool requestOnly );
typedef void (*OwnRemoveSearchPathsFn)( void *fs, const char *pathID );
typedef bool (*OwnIsDirectoryFn)( void *fs, const char *name, const char *pathID );
typedef const char * (*OwnFindFirstExFn)( void *fs, const char *wildcard, const char *pathID,
                                          int *handle );
typedef const char * (*OwnFindNextFn)( void *fs, int handle );
typedef bool (*OwnFindIsDirectoryFn)( void *fs, int handle );
typedef void (*OwnFindCloseFn)( void *fs, int handle );

inline void **base_table( void ) { return *(void ***)g_fs_base; }
inline void **own_table( void )  { return *(void ***)g_fs; }

/* {==== string and path helpers ========================================} */

int ascii_iequal( const char *a, const char *b )
{
  while ( *a != '\0' && *b != '\0' ) {
    int ca = ( *a >= 'A' && *a <= 'Z' ) ? *a + ( 'a' - 'A' ) : *a;
    int cb = ( *b >= 'A' && *b <= 'Z' ) ? *b + ( 'a' - 'A' ) : *b;
    if ( ca != cb ) return 0;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

/* Case-insensitive prefix test. */
bool ascii_iprefix( const char *text, const char *prefix )
{
  while ( *prefix != '\0' ) {
    if ( *text == '\0' ) return false;
    int ca = ( *text >= 'A' && *text <= 'Z' ) ? *text + ( 'a' - 'A' ) : *text;
    int cb = ( *prefix >= 'A' && *prefix <= 'Z' ) ? *prefix + ( 'a' - 'A' ) : *prefix;
    if ( ca != cb ) return false;
    text++;
    prefix++;
  }
  return true;
}

std::string lower_ascii( const std::string &in )
{
  std::string out( in );
  for ( size_t i = 0; i < out.size(); i++ ) {
    const char c = out[i];
    if ( c >= 'A' && c <= 'Z' ) out[i] = (char)( c + ( 'a' - 'A' ) );
  }
  return out;
}

/* Fold a name to the spelling everything below agrees on: forward slashes,
** repeated separators collapsed, "." dropped, ".." applied to a previous
** component.  A leading ".." is preserved: callers reach above the content
** directory with it. */
std::string normalise_path( const char *text )
{
  std::vector<std::string> parts;
  if ( text == NULL ) return std::string();

  const size_t n = strlen( text );
  size_t i = 0;
  while ( i < n ) {
    while ( i < n && ( text[i] == '/' || text[i] == '\\' ) ) i++;
    if ( i >= n ) break;
    const size_t start = i;
    while ( i < n && text[i] != '/' && text[i] != '\\' ) i++;
    const size_t len = i - start;

    if ( len == 1 && text[start] == '.' ) continue;
    if ( len == 2 && text[start] == '.' && text[start + 1] == '.' ) {
      if ( !parts.empty() && parts.back() != ".." ) parts.pop_back();
      else parts.push_back( std::string( text + start, len ) );
      continue;
    }
    parts.push_back( std::string( text + start, len ) );
  }

  std::string out;
  for ( size_t k = 0; k < parts.size(); k++ ) {
    if ( k != 0 ) out.push_back( '/' );
    out.append( parts[k] );
  }
  return out;
}

/* The stored name is the resolved script name with a leading workshop prefix
** removed, which is the spelling the reference records for content reached
** through a workshop mount. */
std::string cache_key( const char *text )
{
  const std::string norm = normalise_path( text );
  if ( norm.size() > 9 && ascii_iprefix( norm.c_str(), "workshop/" ) ) {
    return norm.substr( 9 );
  }
  return norm;
}

/* {==== guarded calls into the engine object =========================== */

/* Each helper answers with the empty value and takes the interface out of
** service when a call misbehaves, so the caller can fall back to the local
** walk instead of propagating the failure. */

bool fs_exists( const std::string &name, const char *pathID )
{
  if ( g_fs_base == NULL || !g_fs_usable || name.empty() ) return false;
  int result = 0;
#ifdef _WIN32
  __try {
#endif
    result = ( (BaseFileExistsFn)base_table()[kBaseFileExists] )
               ( g_fs_base, name.c_str(), pathID ) ? 1 : 0;
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
    result = 0;
  }
#endif
  return result != 0;
}

long long fs_filetime( const std::string &name, const char *pathID )
{
  if ( g_fs_base == NULL || !g_fs_usable || name.empty() ) return 0;
  long long result = 0;
#ifdef _WIN32
  __try {
#endif
    result = ( (BaseGetFileTimeFn)base_table()[kBaseGetFileTime] )
               ( g_fs_base, name.c_str(), pathID );
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
    result = 0;
  }
#endif
  return result;
}

/* Whole file through Open / Size / Read / Close on the base block.  The handle
** stays inside the engine, so no engine-owned buffer crosses the module
** boundary and no allocator is crossed. */
int fs_read( const std::string &name, const char *pathID, std::string &out )
{
  int total = 0;
  if ( g_fs_base == NULL || !g_fs_usable || name.empty() ) return 0;

#ifdef _WIN32
  __try {
#endif
    void *handle = ( (BaseOpenFn)base_table()[kBaseOpen] )( g_fs_base, name.c_str(), "rb", pathID );
    if ( handle != NULL ) {
      const unsigned int size = ( (BaseSizeFn)base_table()[kBaseSizeHandle] )( g_fs_base, handle );
      if ( size != 0 ) {
        out.assign( (size_t)size, '\0' );
        const BaseReadFn readfile = (BaseReadFn)base_table()[kBaseRead];
        while ( total < (int)size ) {
          const int got = readfile( g_fs_base, &out[(size_t)total], (int)size - total, handle );
          if ( got <= 0 ) break;
          total += got;
        }
        out.resize( (size_t)total );
      }
      ( (BaseCloseFn)base_table()[kBaseClose] )( g_fs_base, handle );
    }
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
    total = 0;
  }
#endif
  return total;
}

/* Directory existence.  The reference's own existence test accepts a
** directory, and this is the call it makes for that question. */
bool fs_isdir( const std::string &name, const char *pathID )
{
  if ( g_fs == NULL || !g_fs_usable || name.empty() ) return false;
  int result = 0;
#ifdef _WIN32
  __try {
#endif
    result = ( (OwnIsDirectoryFn)own_table()[kOwnIsDirectory] )
               ( g_fs, name.c_str(), pathID ) ? 1 : 0;
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
    result = 0;
  }
#endif
  return result != 0;
}

/* One directory entry as reported by the engine walk; plain bytes, so the
** guarded call needs no object lifetime handling. */
struct RawFind {
  char name[260];
  int  isFolder;
};

/* Wildcard walk.  Returns the number of entries written; a misbehaving
** interface reports a negative count after taking itself out of service. */
int fs_walk( const std::string &pattern, const char *pathID, RawFind *out, int cap )
{
  int count = 0;
  if ( g_fs == NULL || !g_fs_usable || pattern.empty() ) return -1;

#ifdef _WIN32
  __try {
#endif
    int handle = -1;
    const char *name = ( (OwnFindFirstExFn)own_table()[kOwnFindFirstEx] )
                         ( g_fs, pattern.c_str(), pathID, &handle );
    while ( name != NULL && count < cap ) {
      /* Dot entries are directory bookkeeping and hidden files; the reference
      ** walk skips them before reporting anything. */
      if ( name[0] != '\0' && name[0] != '.' ) {
        size_t n = strlen( name );
        if ( n >= sizeof( out[0].name ) ) n = sizeof( out[0].name ) - 1;
        memcpy( out[count].name, name, n );
        out[count].name[n] = '\0';
        out[count].isFolder =
          ( (OwnFindIsDirectoryFn)own_table()[kOwnFindIsDirectory] )( g_fs, handle ) ? 1 : 0;
        count++;
      }
      name = ( (OwnFindNextFn)own_table()[kOwnFindNext] )( g_fs, handle );
    }
    if ( handle != -1 ) {
      ( (OwnFindCloseFn)own_table()[kOwnFindClose] )( g_fs, handle );
    }
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
    count = -1;
  }
#endif
  return count;
}

} /* namespace */

/* ------------------------------------------------------------------------ */
/* LuaFile cache.                                                           */
/*                                                                          */
/* Every resolved script is kept here and the same object is handed back     */
/* while its recorded timestamp still matches the file.  Callers compare     */
/* pointers between two loads of one script, and the engine bumps the        */
/* per-realm load counters on the object it receives, so a reload has to     */
/* reuse the entry rather than rebuild it.                                   */

namespace {

/* The shipping LuaFile layout.  The public header is a smaller mirror; the
** engine allocates the full block and reads both load counters from its tail,
** so the size and the field order are asserted here. */
struct GmodLuaFile {
  int time;
  std::string name;
  std::string source;
  std::string contents;
  unsigned char compressed[0x20];
  unsigned char reserved[0x08];
  unsigned int timesloadedserver;
  unsigned int timesloadedclient;
};

static_assert( sizeof(GmodLuaFile) == 0x98, "LuaFile size must match the engine" );
static_assert( offsetof(GmodLuaFile, name) == 0x08, "name offset" );
static_assert( offsetof(GmodLuaFile, source) == 0x28, "source offset" );
static_assert( offsetof(GmodLuaFile, contents) == 0x48, "contents offset" );
static_assert( offsetof(GmodLuaFile, compressed) == 0x68, "compressed offset" );
static_assert( offsetof(GmodLuaFile, timesloadedserver) == 0x90, "server counter" );
static_assert( offsetof(GmodLuaFile, timesloadedclient) == 0x94, "client counter" );

std::vector<GmodLuaFile *> g_cache;

bool same_name( const std::string &a, const std::string &b )
{
  return a.size() == b.size() && ascii_iequal( a.c_str(), b.c_str() ) != 0;
}

GmodLuaFile *cache_lookup( const std::string &key )
{
  for ( size_t i = 0; i < g_cache.size(); i++ ) {
    if ( same_name( g_cache[i]->name, key ) ) return g_cache[i];
  }
  return NULL;
}

/* Fetch or create the entry for `key`, then refresh its contents and
** timestamp.  The counters belong to the engine -- they are zeroed only when
** the entry is created and keep counting across reloads -- and `source` stays
** empty for a file read, which is what the reference does for scripts that
** were not transferred over the network.  The compressed copy holds data only
** for transferred scripts, so a file read leaves it empty. */
GmodLuaFile *cache_store( const std::string &key, const std::string &contents, unsigned int stamp )
{
  GmodLuaFile *file = cache_lookup( key );
  if ( file == NULL ) {
    file = new GmodLuaFile();
    file->name = key;
    file->source.clear();
    file->contents.clear();
    file->timesloadedserver = 0;
    file->timesloadedclient = 0;
    g_cache.push_back( file );
  }
  file->contents = contents;
  file->time = (int)stamp;
  memset( file->compressed, 0, sizeof(file->compressed) );
  memset( file->reserved, 0, sizeof(file->reserved) );
  return file;
}

bool find_result_less( const GarrysMod::Lua::LuaFindResult &a,
                       const GarrysMod::Lua::LuaFindResult &b )
{
  return a.fileName < b.fileName;
}

/* Directory-walk callback: the C side reports base names plus a folder flag. */
void collect_entry( void *ctx, const char *name, int isFolder )
{
  std::vector<GarrysMod::Lua::LuaFindResult> *out =
    (std::vector<GarrysMod::Lua::LuaFindResult> *)ctx;
  if ( out == NULL || name == NULL || name[0] == '\0' || name[0] == '.' ) return;
  GarrysMod::Lua::LuaFindResult r;
  r.fileName = name;
  r.isFolder = ( isFolder != 0 );
  out->push_back( r );
}

/* Resolve and read one script.  The interface answers first; the local walk
** covers the standalone build and every root the engine mounted somewhere this
** module was not told about.  A path id is honoured by the interface, and the
** walk searches every recorded root, which is the conservative answer for a
** name it cannot place. */
bool read_script( const std::string &name, const std::string &pathID,
                  std::string &out, unsigned int *stamp )
{
  if ( stamp != NULL ) *stamp = 0;
  if ( name.empty() ) return false;

  if ( g_fs_base != NULL && g_fs_usable ) {
    const char *ids[2];
    int count = 0;
    ids[count++] = pathID.empty() ? NULL : pathID.c_str();
    if ( !pathID.empty() ) ids[count++] = NULL;

    for ( int k = 0; k < count; k++ ) {
      /* The timestamp doubles as the existence test the reference uses: a
      ** script the interface cannot date is not one it can hand out. */
      const long long t = fs_filetime( name, ids[k] );
      if ( !g_fs_usable ) break;
      if ( t == 0 && !fs_exists( name, ids[k] ) ) continue;

      if ( fs_read( name, ids[k], out ) <= 0 ) {
        if ( !g_fs_usable ) break;
        continue;
      }
      if ( stamp != NULL ) *stamp = (unsigned int)t;
      return true;
    }
  }

  char *data = NULL;
  const int len = opglua_read_script_id( name.c_str(),
                                        pathID.empty() ? NULL : pathID.c_str(), &data );
  if ( len > 0 && data != NULL ) out.assign( data, (size_t)len );
  if ( data != NULL ) free( data );
  return len > 0 && !out.empty();
}

} /* namespace */

/* The reference defines the LuaFile destructor in this module; the engine's own
** free path may invoke it, so keep a definition. */
namespace GarrysMod { namespace Lua {
LuaFile::~LuaFile() {}
}}

/* ------------------------------------------------------------------------ */
/* Entry points used by the ILuaShared slots.                               */

/* Bind (or unbind) the engine file system and publish it through the exported
** pointer.  Init passes the interface the factory returned; Shutdown passes
** NULL, which also drops the cached scripts. */
extern "C" void opglua_luashared_setfilesystem( void *fs )
{
  g_fs = fs;
  g_fs_base = ( fs != NULL ) ? ( (char *)fs + sizeof( void * ) ) : NULL;
  g_fs_usable = true;
#ifdef _WIN32
  g_pFullFileSystem = (IFileSystem *)fs;
#endif
  if ( fs == NULL ) g_cache.clear();
}

extern "C" bool opglua_luashared_hasfilesystem( void )
{
  return g_fs_base != NULL && g_fs_usable;
}

/* Engine-only byte read for the script layer: the interface answers first with
** the caller's path id, then without one (every mounted path, including the
** packed archives).  No local fallback here -- the callers layer their own
** disk walk on top.  Returns the byte length and a malloc'd buffer, or 0. */
extern "C" int opglua_luashared_readfile( const char *name, const char *pathID,
                                          char **outData )
{
  std::string bytes;
  int total = 0;
  if ( outData != NULL ) *outData = NULL;
  if ( name == NULL || name[0] == '\0' ) return 0;
  if ( g_fs_base == NULL || !g_fs_usable ) return 0;

  if ( pathID != NULL && pathID[0] != '\0' ) {
    total = fs_read( std::string( name ), pathID, bytes );
    if ( !g_fs_usable ) return 0;
  }
  if ( total <= 0 || bytes.empty() ) {
    total = fs_read( std::string( name ), NULL, bytes );
    if ( !g_fs_usable ) return 0;
  }
  if ( total <= 0 || bytes.empty() ) return 0;

  {
    char *buf = (char *)malloc( (size_t)total + 1 );
    if ( buf == NULL ) return 0;
    memcpy( buf, bytes.data(), (size_t)total );
    buf[total] = '\0';
    if ( outData != NULL ) *outData = buf; else free( buf );
  }
  return total;
}

/* Engine-only existence test: with the caller's path id, then without one. */
extern "C" int opglua_luashared_fileexists( const char *name, const char *pathID )
{
  if ( name == NULL || name[0] == '\0' ) return 0;
  if ( g_fs_base == NULL || !g_fs_usable ) return 0;
  if ( pathID != NULL && pathID[0] != '\0' && fs_exists( std::string( name ), pathID ) )
    return 1;
  if ( !g_fs_usable ) return 0;
  return fs_exists( std::string( name ), NULL ) ? 1 : 0;
}

/* Engine-only directory test. */
extern "C" int opglua_luashared_isdirectory( const char *name, const char *pathID )
{
  if ( name == NULL || name[0] == '\0' ) return 0;
  if ( g_fs == NULL || !g_fs_usable ) return 0;
  return fs_isdir( std::string( name ), pathID ) ? 1 : 0;
}

/* LoadFile(name, pathID, fromDatatable, fromFile) -> LuaFile* or NULL.
**
** Non-NULL is exactly what the engine's gamemode existence test accepts, so
** the NULL conditions matter as much as the success path:
**
**   - the name is empty or collapses to nothing,
**   - neither the interface nor the local walk finds the script,
**   - the script reads back with no bytes (an empty file is not loadable).
**
** The two flags pick between the transferred copy and the mounted file in the
** reference.  This host implements no client file transfer, and the two copies
** hold the same bytes when the file is mounted at all, so both combinations
** resolve the same way: a cached entry whose timestamp still matches the file
** is returned untouched, otherwise the bytes are re-read and stored on the same
** object.
**
** Field fill follows the reference: `name` is the script name lower-cased with
** a leading workshop prefix removed (the same key the cache lookup uses),
** `contents` holds the bytes, `source` is empty for a file read, the compressed
** copy is empty, both load counters start at zero, and `time` carries the file
** timestamp so a reload can tell whether the bytes changed.
*/
extern "C" void *opglua_luashared_loadfile( const void *path, const void *pathId,
                                           bool fromDatatable, bool fromFile )
{
  (void)fromDatatable; (void)fromFile;
  if ( path == NULL ) return NULL;

  /* The file questions run against the name as given (normalized); the cache
  ** identity and the stored name are the stripped, lower-cased spelling. */
  const std::string norm = normalise_path( ( (const std::string *)path )->c_str() );
  const std::string key = lower_ascii( cache_key( ( (const std::string *)path )->c_str() ) );
  if ( norm.empty() || key.empty() ) return NULL;
  const std::string pid = ( pathId != NULL )
                            ? normalise_path( ( (const std::string *)pathId )->c_str() )
                            : std::string();

  GmodLuaFile *cached = cache_lookup( key );

  /* A cached copy that still matches the file needs no read at all. */
  if ( cached != NULL && !cached->contents.empty() ) {
    if ( g_fs_base == NULL || !g_fs_usable ) return cached;
    const long long t = fs_filetime( norm, pid.empty() ? NULL : pid.c_str() );
    if ( !g_fs_usable ) return cached;
    if ( t != 0 && (unsigned int)cached->time == (unsigned int)t ) return cached;
  }

  unsigned int stamp = 0;
  std::string bytes;
  if ( !read_script( norm, pid, bytes, &stamp ) || bytes.empty() ) return NULL;
  return (void *)cache_store( key, bytes, stamp );
}

/* GetCache(name): a pure cache lookup.  No file is read here -- LoadFile
** populates the table, and a name that was never loaded is a miss, which is
** what the reference answers too. */
extern "C" void *opglua_luashared_getcache( const void *fileName )
{
  if ( fileName == NULL ) return NULL;
  const std::string key = lower_ascii( cache_key( ( (const std::string *)fileName )->c_str() ) );
  if ( key.empty() ) return NULL;
  return (void *)cache_lookup( key );
}

/* EmptyCache(): release every cached entry.  The engine is done with them once
** it asks for this, which is the lifetime the reference gives them. */
extern "C" void opglua_luashared_emptycache( void )
{
  for ( size_t i = 0; i < g_cache.size(); i++ ) delete g_cache[i];
  g_cache.clear();
}

/* InvalidateCache(name): clear the timestamp of the named entries so the next
** LoadFile re-reads the bytes. */
extern "C" void opglua_luashared_invalidatecache( const void *name )
{
  if ( name == NULL ) return;
  const std::string key = lower_ascii( cache_key( ( (const std::string *)name )->c_str() ) );
  if ( key.empty() ) return;
  for ( size_t i = 0; i < g_cache.size(); i++ ) {
    if ( same_name( g_cache[i]->name, key ) ) g_cache[i]->time = 0;
  }
}

/* ScriptExists(file, pathID, unknown) -> bool.  A file the interface knows
** counts, and so does a directory of that name -- the reference answers both
** ways, which is what lets the engine probe "entities/foo" before it asks for
** "entities/foo/shared.lua".  With the flag set only the directory question is
** asked. */
extern "C" bool opglua_luashared_scriptexists( const void *file, const void *path,
                                              bool unknown )
{
  if ( file == NULL ) return false;

  const std::string name = normalise_path( ( (const std::string *)file )->c_str() );
  if ( name.empty() ) return false;
  const std::string pid = ( path != NULL )
                            ? normalise_path( ( (const std::string *)path )->c_str() )
                            : std::string();
  const char *pidText = pid.empty() ? NULL : pid.c_str();

  if ( !unknown ) {
    if ( fs_exists( name, pidText ) ) return true;
    if ( !pid.empty() && fs_exists( name, NULL ) ) return true;
    if ( opglua_script_exists_id( name.c_str(), pidText ) != 0 ) return true;
  }
  if ( fs_isdir( name, pidText ) ) return true;
  return opglua_script_isdir_id( name.c_str(), pidText ) != 0;
}

/* FindScripts(path, pathID, out): append the matching entries and leave the
** caller's vector ordered by file name -- the shape the reference's folder
** walk produces.  Entries are base names with a folder flag (never full
** paths), names starting with a dot are skipped, and a name that appears under
** more than one root is reported once.
**
** The pattern keeps its wildcard part the way the engine's walk expects it
** ("entities/*" -> directory "entities", wildcard "*"), so the engine can build
** "entities/<name>/shared.lua" from what comes back.
*/
extern "C" void opglua_luashared_findscripts( const void *path, const void *pathID,
                                             void *outVector )
{
  if ( outVector == NULL ) return;

  std::vector<GarrysMod::Lua::LuaFindResult> *out =
    (std::vector<GarrysMod::Lua::LuaFindResult> *)outVector;
  const std::string pattern = ( path != NULL )
                                ? normalise_path( ( (const std::string *)path )->c_str() )
                                : std::string();
  const std::string pid = ( pathID != NULL )
                            ? normalise_path( ( (const std::string *)pathID )->c_str() )
                            : std::string();
  if ( pattern.empty() ) return;

  std::vector<GarrysMod::Lua::LuaFindResult> found;

  /* The engine's walk sees every mounted root, addon folder and packed
  ** archive in one pass. */
  if ( g_fs != NULL && g_fs_usable ) {
    enum { kMaxEntries = 8192 };
    RawFind *raw = (RawFind *)malloc( sizeof(RawFind) * kMaxEntries );
    if ( raw != NULL ) {
      const int count = fs_walk( pattern, pid.empty() ? NULL : pid.c_str(),
                                 raw, kMaxEntries );
      for ( int i = 0; i < count; i++ ) {
        GarrysMod::Lua::LuaFindResult r;
        r.fileName = raw[i].name;
        r.isFolder = ( raw[i].isFolder != 0 );
        found.push_back( r );
      }
      free( raw );
    }
  }

  /* The local walk covers what the interface does not answer for: the
  ** standalone build, roots recorded here that the engine did not take, and
  ** any root the engine mounted somewhere this module was not told about. */
  opglua_find_scripts( pattern.c_str(), pid.empty() ? NULL : pid.c_str(),
                       &found, collect_entry );

  /* The reference keeps the reported names in a set and skips a name it has
  ** already handed out, so a script reachable from two roots appears once. */
  std::set<std::string> seen;
  for ( size_t i = 0; i < out->size(); i++ ) seen.insert( (*out)[i].fileName );
  for ( size_t i = 0; i < found.size(); i++ ) {
    if ( seen.insert( found[i].fileName ).second ) out->push_back( found[i] );
  }

  std::sort( out->begin(), out->end(), find_result_less );
}

/* ------------------------------------------------------------------------ */
/* Search path mounting.                                                    */
/*                                                                          */
/* The interface's own table holds the search path calls next to the file    */
/* system methods: AddSearchPath, RemoveSearchPaths and                      */
/* MarkPathIDByRequestOnly.  Their indices sit after the app system head and  */
/* the file open/info methods, and are the ones the reference mounts call.   */
/* Only those three are used; the directory walk stays on the local side.    */

namespace {

/* Add modes the engine defines: appended to the search order, and prepended
** for content that must win over the ordinary roots. */
enum {
  kPathAddToHead = 0x105,
  kPathAddToTail = 5
};

} /* namespace */

/* The host executable sits at bin/win64/<exe> (or bin/<exe>, or the game root
** on a dedicated server); the module directory is <game root>/garrysmod. */
void mod_directory( char *out, size_t outSize )
{
  out[0] = '\0';
  if ( outSize == 0 ) return;
#ifdef _WIN32
  char exe[MAX_PATH];
  const DWORD n = GetModuleFileNameA( NULL, exe, (DWORD)sizeof(exe) );
  if ( n == 0 || n >= sizeof(exe) ) return;

  /* Cut the file name, then up to two more components when they are the
  ** conventional bin/win64 tail. */
  char *cuts[3];
  int cutCount = 0;
  char *end = exe + n;
  int pass;
  for ( pass = 0; pass < 3; pass++ ) {
    char *sep = end;
    while ( sep > exe && *sep != '\\' && *sep != '/' ) sep--;
    if ( sep == exe ) break;
    *sep = '\0';
    cuts[cutCount++] = sep;
    end = sep;
    if ( pass < 2 ) {
      const char *tail = exe;
      const char *p;
      for ( p = exe; *p != '\0'; p++ ) {
        if ( *p == '\\' || *p == '/' ) tail = p + 1;
      }
      const char *want = ( pass == 0 ) ? "win64" : "bin";
      if ( !ascii_iequal( tail, want ) ) break;
    }
  }
  if ( cutCount == 0 ) { snprintf( out, outSize, "%s", exe ); return; }
  snprintf( out, outSize, "%s/garrysmod", exe );
#endif
}

/* The two mount calls, in a frame of their own: the guarded call must not
** share a function with an object that unwinds. */
static void mount_calls( const char *path, const char *pathID, int addType )
{
#ifdef _WIN32
  __try {
#endif
    ( (OwnAddSearchPathFn)own_table()[kOwnAddSearchPath] )( g_fs, path, pathID, addType );
    ( (OwnMarkPathIDFn)own_table()[kOwnMarkPathIDByRequestOnly] )( g_fs, pathID, true );
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
  }
#endif
}

/* MountLuaAdd(root, pathID): add one root to the path id's search set and then
** mark that id as consulted only on request -- both calls are the ones the
** reference makes on this object.  The root is resolved before it is added:
** the directory spelling the file system can already open wins, otherwise the
** absolute "<mod dir>/<root>" -- the reference never hands it a bare relative
** name.  The local walk recorded the same name, so a host without a file
** system still resolves the directory. */
extern "C" void opglua_luashared_mount_add( const char *root, const char *pathID )
{
  if ( root == NULL || root[0] == '\0' || pathID == NULL || pathID[0] == '\0' ) return;
  if ( g_fs == NULL || !g_fs_usable ) return;

  const int addType = ( strncmp( root, "workshop", 8 ) == 0 ) ? kPathAddToHead
                                                              : kPathAddToTail;

  char modDir[MAX_PATH];
  mod_directory( modDir, sizeof(modDir) );
  const std::string rel = normalise_path( root );
  std::string cand;
  if ( modDir[0] != '\0' ) {
    const std::string abs = std::string( modDir ) + "/" + rel;
    if ( fs_isdir( abs, NULL ) ) cand = abs;
    else if ( fs_isdir( rel, NULL ) ) cand = rel;
    else cand = abs;
  } else {
    cand = rel;
  }
  if ( cand.empty() ) return;
  mount_calls( cand.c_str(), pathID, addType );
}

/* UnMountLua(pathID): drop every search path registered under that id, which
** is what the reference clears when a gamemode goes away. */
extern "C" void opglua_luashared_unmount( const char *pathID )
{
  if ( pathID == NULL || pathID[0] == '\0' ) return;
  if ( g_fs == NULL || !g_fs_usable ) return;
#ifdef _WIN32
  __try {
#endif
    ( (OwnRemoveSearchPathsFn)own_table()[kOwnRemoveSearchPaths] )( g_fs, pathID );
#ifdef _WIN32
  } __except ( EXCEPTION_EXECUTE_HANDLER ) {
    g_fs_usable = false;
  }
#endif
}

/* ------------------------------------------------------------------------ */
/* std::string destination helpers for the ILuaInterface slots that fill a  */
/* std::string& out parameter (GetCurrentFile / GetCurrentLocation / ...).  */
/* The caller uses the Source string layout: inline buffer first, size and   */
/* capacity after it.  Heap allocations route through the engine allocator, */
/* so the caller can destroy the string safely.                             */

struct GmodStdStringRef {
  union {
    char buf[16];
    char *ptr;
  } bx;
  size_t size;
  size_t res;
};

extern "C" void *opglua_mem_alloc( size_t n );
extern "C" void  opglua_mem_free( void *p );

extern "C" void opglua_stdstring_clear( void *dest )
{
  if ( dest == NULL ) return;
  GmodStdStringRef *s = (GmodStdStringRef *)dest;
  if ( s->res >= 16 && s->bx.ptr != NULL ) {
    opglua_mem_free( s->bx.ptr );
  }
  s->bx.buf[0] = '\0';
  s->size = 0;
  s->res = 15;
}

extern "C" void opglua_stdstring_assign( void *dest, const char *data, unsigned int len )
{
  if ( dest == NULL ) return;
  GmodStdStringRef *s = (GmodStdStringRef *)dest;
  if ( data == NULL ) len = 0;
  if ( s->res >= 16 && s->bx.ptr != NULL ) {
    opglua_mem_free( s->bx.ptr );
    s->bx.buf[0] = '\0';
    s->res = 15;
  }
  if ( len <= 15 ) {
    if ( len > 0 ) memcpy( s->bx.buf, data, len );
    s->bx.buf[len] = '\0';
    s->size = len;
    s->res = 15;
  } else {
    char *p = (char *)opglua_mem_alloc( len + 1 );
    if ( p != NULL ) {
      memcpy( p, data, len );
      p[len] = '\0';
      s->bx.ptr = p;
      s->size = len;
      s->res = len;
    } else {
      s->bx.buf[0] = '\0';
      s->size = 0;
      s->res = 15;
    }
  }
}
