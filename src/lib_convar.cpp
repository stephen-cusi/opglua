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

extern "C" void *glua_convars_create_convar( const char *name, const char *def,
                                             const char *help, int flags )
{
  return (void *)new ConVar( name, def, flags, help );
}

extern "C" void *glua_convars_create_concommand( const char *name, const char *help,
                                                int flags, void *callback,
                                                void *completion )
{
  FnCommandCallback_t cb = (FnCommandCallback_t)callback;
  FnCommandCompletionCallback comp = (FnCommandCompletionCallback)completion;
  return (void *)new ConCommand( name, cb, help, flags, comp );
}
