/*
** ConVar / ConCommand creation for the Garry's Mod platform layer.
**
** The reference build statically links the engine's ConVar implementation, so
** ILuaInterface::CreateConVar / CreateConCommand hand the engine a real
** ConVar/ConCommand (which self-registers with the engine's ICVar).  This
** translation unit is the C++ bridge: lib_gmod.c declares the two entry
** points below and calls them from slots 126/127.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <vector>

#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "icvar.h"

/* The engine's interface factory pulls VEngineCvar007; the reference stores it
** into the convar module's g_pCVar global so every ConVar/ConCommand it builds
** self-registers with the engine and value changes reach the engine tracker.
** Without this, a later SetValue walks a NULL g_pCVar and faults. */
extern "C" void opglua_convars_set_cvar( void *cvar )
{
  g_pCVar = (ICvar *)cvar;
}

/* ------------------------------------------------------------------------ */
/* Managed objects.                                                         */
/*                                                                          */
/* A ConVar a script creates is owned by the ConVar module: the engine keeps */
/* it registered, so the module has to be the one that takes it out again    */
/* when the last realm closes, otherwise a map change leaves stale objects   */
/* in the engine's list.                                                     */

namespace {
std::vector<void *> g_managed;

void track( void *obj )
{
  if ( obj != NULL ) g_managed.push_back( obj );
}
} /* namespace */

extern "C" void glua_convars_destroy_managed( void )
{
  for ( size_t i = 0; i < g_managed.size(); i++ ) {
    delete (ConCommandBase *)g_managed[i];
  }
  g_managed.clear();
}

extern "C" void *glua_convars_create_convar( const char *name, const char *def,
                                             const char *help, int flags )
{
  {
    ConVar *cv = new ConVar( name, def, flags, help );
    track( cv );
    return (void *)cv;
  }
}

extern "C" void *glua_convars_create_concommand( const char *name, const char *help,
                                                int flags, void *callback,
                                                void *completion )
{
  FnCommandCallback_t cb = (FnCommandCallback_t)callback;
  FnCommandCompletionCallback comp = (FnCommandCompletionCallback)completion;
  {
    ConCommand *cc = new ConCommand( name, cb, help, flags, comp );
    track( cc );
    return (void *)cc;
  }
}

/* Diagnostic variant: report whether the engine cvar system is bound before the
** object is built, and hand the caller the pointer it produced. */
extern "C" void glua_convars_create_convar_probe( const char *name, const char *def,
                                                  const char *help, int flags, void **out )
{
  ConVar *cv;
  if ( out != NULL ) *out = NULL;
  if ( g_pCVar == NULL ) {
    if ( out != NULL ) *out = NULL;
    return;
  }
  cv = new ConVar( name ? name : "", def ? def : "", flags, help ? help : "" );
  if ( cv != NULL && cv->GetName() == NULL ) {
    /* Registration leaves the base object linked into the engine list; a name
    ** that vanished means the object was not stored. */
    if ( out != NULL ) *out = NULL;
    return;
  }
  if ( out != NULL ) *out = cv;
}

