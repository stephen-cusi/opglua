/*
** Route this module's C++ allocations through the engine allocator, and trace
** outsized requests so a runaway allocation names its caller.
**
** The reference Lua VM links the engine tier0 allocator (g_pMemAlloc) and
** routes every operator new/delete through it.  Objects cross the module
** boundary (a LuaFile from ILuaShared::LoadFile, a ConVar from CreateConVar,
** the buffer of a std::string& out parameter) and the ENGINE frees them with
** its own operator delete; allocating with the CRT allocator instead makes the
** engine's free walk a foreign block header and fault.  Binding to g_pMemAlloc
** keeps both sides on one allocator.
**
** For bring-up the module also wraps g_pMemAlloc's Alloc entry with a
** transparent trampoline that records every request together with its call
** stack, so a runaway allocation identifies its caller.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <windows.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <new>

/* IMemAlloc release vtable: [0]=Alloc(size) [1]=Realloc(p,size) [2]=Free(p). */
typedef void *(*fn_alloc)(void *, size_t);
typedef void *(*fn_realloc)(void *, void *, size_t);
typedef void  (*fn_free)(void *, void *);

static fn_alloc   g_orig_alloc = NULL;
static fn_realloc g_orig_realloc = NULL;
static fn_free    g_orig_free = NULL;
static void      *g_memalloc_self = NULL;
static int        g_bound = 0;

static void memlog( const char *fmt, ... )
{
  static FILE *f = NULL;
  static int tried = 0;
  va_list ap;
  if (!tried) { tried = 1; f = fopen("D:\\tmp\\glua\\opglua_bigalloc.log", "a"); }
  if (f == NULL) return;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fflush(f);
}

static void log_stack( FILE *f, const char *tag, void *addr )
{
  HMODULE mod = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)addr, &mod) && mod != NULL) {
    char name[256];
    if (GetModuleFileNameA(mod, name, sizeof(name))) {
      const char *base = name;
      char *s;
      for (s = name; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
      fprintf(f, "%s %s+0x%llx\n", tag, base, (unsigned long long)((char *)addr - (char *)mod));
      return;
    }
  }
  fprintf(f, "%s <unknown> %p\n", tag, addr);
}

static void bind_allocator()
{
  HMODULE tier0;
  void **p;
  if (g_bound) return;
  g_bound = 1;
  tier0 = GetModuleHandleA("tier0.dll");
  if (tier0 == NULL) tier0 = LoadLibraryA("tier0.dll");
  if (tier0 == NULL) return;
  p = (void **)GetProcAddress(tier0, "g_pMemAlloc");
  if (p == NULL || *p == NULL) return;
  {
    void **vt = *(void ***)(*p);
    g_memalloc_self = *p;
    g_orig_alloc = (fn_alloc)vt[0];
    g_orig_realloc = (fn_realloc)vt[2];
    g_orig_free = (fn_free)vt[4];
  }
}

void *operator new( size_t n )
{
  bind_allocator();
  if (g_orig_alloc) { void *p = g_orig_alloc(g_memalloc_self, n ? n : 1); if (p) return p; }
  return malloc( n ? n : 1 );
}
void *operator new[]( size_t n ) { return operator new( n ); }
void *operator new( size_t n, const std::nothrow_t & ) throw() { return operator new( n ); }
void *operator new[]( size_t n, const std::nothrow_t & ) throw() { return operator new( n ); }

void operator delete( void *p ) throw()
{
  bind_allocator();
  if (p == NULL) return;
  if (g_orig_free) { g_orig_free(g_memalloc_self, p); return; }
  free( p );
}
void operator delete[]( void *p ) throw() { operator delete( p ); }
void operator delete( void *p, const std::nothrow_t & ) throw() { operator delete( p ); }
void operator delete[]( void *p, const std::nothrow_t & ) throw() { operator delete( p ); }
void operator delete( void *p, size_t ) throw() { operator delete( p ); }
void operator delete[]( void *p, size_t ) throw() { operator delete( p ); }

extern "C" void *opglua_mem_alloc( size_t n )
{
  bind_allocator();
  if (g_orig_alloc) return g_orig_alloc(g_memalloc_self, n ? n : 1);
  return malloc( n ? n : 1 );
}

extern "C" void opglua_mem_free( void *p )
{
  if (p == NULL) return;
  bind_allocator();
  if (g_orig_free) { g_orig_free(g_memalloc_self, p); return; }
  free( p );
}

/* Let the platform layer install the trampoline as early as possible (from
** ILuaShared::Init), before the engine runs the code we are diagnosing. */
extern "C" void opglua_mem_bind( void ) { bind_allocator(); }
