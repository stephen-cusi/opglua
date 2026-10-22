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
#include <set>
#include <string>

#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "icvar.h"

ICvar *g_pCVar = NULL;

static_assert(sizeof(ConCommandBase) == 0x30, "Console base ABI");
static_assert(sizeof(ConVar) == 0x90, "Console variable ABI");

/* The engine's interface factory pulls VEngineCvar007; the reference stores it
** into the convar module's g_pCVar global so every ConVar/ConCommand it builds
** self-registers with the engine and value changes reach the engine tracker.
** Without this, a later SetValue walks a NULL g_pCVar and faults. */
extern "C" void opglua_convars_set_cvar( void *cvar )
{
  g_pCVar = (ICvar *)cvar;
}

/* Publishing the module's ConCommandBase list.
**
** A ConCommandBase that is built before the engine cvar system is known only
** links itself into this module's static list: the base constructor registers
** with the engine through the accessor, and there is no accessor yet.  The
** published list is handed over once, when the module comes up, by the same
** entry point the reference uses: it walks the list, tags every object with
** this module's identifier and registers it, then sets the accessor so that
** everything built afterwards registers on construction.
**
** Skipping this leaves every script-created ConVar alive but unknown to the
** engine, so a lookup by name from script finds nothing. */
extern "C" void opglua_convars_register( void )
{
  ConVar_Register( 0, NULL );
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
std::set<std::string> g_console_strings;

const char *console_string( const char *text )
{
  return g_console_strings.insert( text ? text : "" ).first->c_str();
}

void track( void *obj )
{
  if ( obj != NULL ) g_managed.push_back( obj );
}
} /* namespace */

/* Release what this module created.
**
** A ConVar registered with the engine is also linked into the engine's own
** global list, which lives in another module.  Deleting the object here leaves
** that list holding a dangling pointer -- the engine then walks it and calls
** through a dead vtable, which is a hard crash.  The reference never does
** this either: its managed vector is cleared but the objects stay registered.
** So the list is emptied and the objects are left alive for the process
** lifetime.
*/
extern "C" void glua_convars_destroy_managed( void )
{
  g_managed.clear();
}

extern "C" void *glua_convars_create_convar( const char *name, const char *def,
                                             const char *help, int flags )
{
  {
    if (g_pCVar != NULL) {
      ConVar *existing = g_pCVar->FindVar(name);
      if (existing != NULL) return existing;
    }
    ConVar *cv = new ConVar( console_string( name ), console_string( def ), flags,
                            console_string( help ) );
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
    ConCommand *cc = new ConCommand( console_string( name ), cb,
                                    console_string( help ), flags, comp );
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
  cv = new ConVar( console_string( name ), console_string( def ), flags,
                   console_string( help ) );
  if ( cv != NULL && cv->GetName() == NULL ) {
    /* Registration leaves the base object linked into the engine list; a name
    ** that vanished means the object was not stored. */
    if ( out != NULL ) *out = NULL;
    return;
  }
  if ( out != NULL ) *out = cv;
}

