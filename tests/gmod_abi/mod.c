/*
** opglua_testmod - minimal Garry's Mod binary module for the VM load test.
**
** Builds against the authoritative GarrysMod/Lua headers only: it declares
** gmod13_open / gmod13_close, pushes a string global and a number global,
** and exposes one ILuaBase-callable function.  It does not link the VM; it
** reaches Lua solely through the ILuaBase vtable handed to gmod13_open.
**
** The module ABI headers are C++ (namespaces, classes, virtual methods), so
** this file must be compiled as C++ (MSVC /TP).  See mod_build.bat.
*/

#include "GarrysMod/Lua/Interface.h"

#include <stdio.h>

GMOD_MODULE_OPEN()
{
	/* Mark that the module entry actually ran. */
	LUA->PushString( "hello-from-opglua-testmod" );
	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_marker" );

	/* A numeric global (exercises PushNumber + SetField). */
	LUA->PushNumber( 1234.5 );
	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_number" );

	/* A boolean global from the module. */
	LUA->PushBool( true );
	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_flag" );

	/* Report the module path argument the loader passed (1 Lua argument). */
	if ( LUA->Top() >= 1 )
	{
		LUA->Push( 1 );
		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_arg" );
	}

	printf( "[opglua_testmod] gmod13_open called\n" );
	fflush( stdout );

	return 0;
}

GMOD_MODULE_CLOSE()
{
	printf( "[opglua_testmod] gmod13_close called\n" );
	fflush( stdout );
	return 0;
}
