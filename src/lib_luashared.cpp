/*
** ILuaShared tail helpers for the Garry's Mod platform layer.
**
** The engine drives several ILuaShared slots through std::string references
** (LoadFile / GetCache / ScriptExists / InvalidateCache / FindScripts), and it
** decides whether a gamemode exists by asking LoadFile for the script and
** checking that it returned a LuaFile.  This translation unit builds those
** std::string-typed answers and hands back a real LuaFile, so the engine sees
** the same shape the reference produces.  The C side (lib_gmod.c) resolves the
** file bytes and calls in through the two extern "C" entry points below.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <string>

#include "GarrysMod/Lua/LuaShared.h"

/* Implemented in lib_gmod.c. */
extern "C" int opglua_read_script( const char *name, char **outData );
extern "C" int opglua_script_exists( const char *name );

namespace GarrysMod { namespace Lua {
/* The header declares this dtor; the reference defines it in its own TU.  We
** provide it here so LuaFile can be constructed/destroyed from this module. */
LuaFile::~LuaFile() {}
}}

/* LoadFile(path, pathId, fromDatatable, fromFile) -> LuaFile* or NULL.
** Returns non-NULL only when the script resolves, which is exactly the
** existence test the engine's gamemode loader performs. */
extern "C" void *opglua_luashared_loadfile( const void *path, const void *pathId,
                                           bool fromDatatable, bool fromFile )
{
  (void)fromDatatable; (void)fromFile;
  if ( path == NULL ) return NULL;
  const std::string &p = *(const std::string *)path;
  const std::string &pid = ( pathId != NULL ) ? *(const std::string *)pathId : p;

  char *data = NULL;
  int len = opglua_read_script( p.c_str(), &data );
  if ( len <= 0 || data == NULL ) return NULL;

  GarrysMod::Lua::LuaFile *file = new GarrysMod::Lua::LuaFile();
  file->time = 0;
  file->name = p;
  file->source = pid;
  file->contents.assign( data, (size_t)len );
  file->compressed.m_pData = NULL;
  file->compressed.m_iSize = 0;
  file->compressed.m_iPos = 0;
  file->compressed.m_iWritten = 0;
  file->timesloadedserver = 1;
  file->timesloadedclient = 0;
  free( data );
  return (void *)file;
}

/* ScriptExists(file, path, unknown) -> bool, via the same resolver. */
extern "C" bool opglua_luashared_scriptexists( const void *file, const void *path,
                                              bool unknown )
{
  (void)path; (void)unknown;
  if ( file == NULL ) return false;
  return opglua_script_exists( (*(const std::string *)file).c_str() ) != 0;
}
