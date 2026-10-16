/*
** opglua_host - end-to-end loader test host.
**
** Drives the full chain the reference engine uses:
**   CreateInterface("LUASHARED003") -> ILuaShared
**   ILuaShared::CreateLuaInterface(SERVER) -> ILuaInterface
**   ILuaInterface::GetState() -> lua_State* (whose luabase is the interface)
**   GMOD_LoadBinaryModule(state, modulePath) -> loads the DLL and calls its
**     gmod13_open with the path string as the single Lua argument
**   ILuaInterface::GetField(INDEX_GLOBAL, ...) -> read the module globals
**
** Build with mod_build.bat.  Compiled as C++ because the module ABI headers
** are C++.
*/

#define GMOD_USE_ILUAINTERFACE 1
#include "GarrysMod/Lua/Interface.h"

#include <stdio.h>
#include <string.h>

/* VM/platform entry points exported by lua51.dll. */
extern "C" void *CreateInterface( const char *name, int *pReturnCode );
extern "C" int GMOD_LoadBinaryModule( lua_State *L, const char *path );

/* Minimal ILuaShared view: same vtable order the VM installs (18 slots). */
class ILuaSharedView
{
public:
	virtual ~ILuaSharedView();
	virtual void Init( void *factory, bool magicBool, void *ctx, void *get );
	virtual void Shutdown();
	virtual void DumpStats();
	virtual GarrysMod::Lua::ILuaInterface *CreateLuaInterface( unsigned char realm, bool unknown );
	virtual void CloseLuaInterface( GarrysMod::Lua::ILuaInterface *iface );
	virtual GarrysMod::Lua::ILuaInterface *GetLuaInterface( unsigned char realm );
	virtual void *LoadFile( const void *path, const void *pathId, bool a, bool b );
	virtual void MountLua( const char *pathID );
	virtual void MountLuaAdd( const char *file, const char *pathID );
	virtual void UnMountLua( const char *pathID );
	virtual void SetFileContents( const char *a, const char *b );
	virtual void SetLuaFindHook( void *hook );
	virtual void FindScripts( const void *path, const void *pathID, void *out );
	virtual const char *GetStackTraces();
	virtual void InvalidateCache( const void *unknown );
	virtual void EmptyCache();
	virtual bool ScriptExists( const void *file, const void *path, bool unknown );
};

static int g_fail = 0;
static void check( const char *what, bool ok )
{
	printf( "[host] %-52s %s\n", what, ok ? "PASS" : "FAIL" );
	if ( !ok ) g_fail++;
}

static const char *getGlobalString( GarrysMod::Lua::ILuaInterface *LUA, const char *name )
{
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, name );
	const char *s = ( LUA->GetType( -1 ) == 4 /* string */ ) ? LUA->GetString( -1 ) : NULL;
	LUA->Pop( 1 );
	return s;
}

int main( int argc, char **argv )
{
	const char *modPath = ( argc > 1 ) ? argv[1] : "testmod.dll";
	int rc = -99;
	void *sharedRaw;
	ILuaSharedView *shared;
	GarrysMod::Lua::ILuaInterface *LUA;
	lua_State *state;
	int loadRc;

	sharedRaw = CreateInterface( "LUASHARED003", &rc );
	check( "CreateInterface returned a non-null interface", sharedRaw != NULL );
	if ( sharedRaw == NULL ) return 1;
	shared = (ILuaSharedView *)sharedRaw;

	/* The engine hands its interface factory to Init before creating
	** interfaces.  A null factory is fine for the standalone path. */
	shared->Init( NULL, false, NULL, NULL );

	/* Unknown interface names return NULL, like the reference list walk. */
	{
		int rc2 = 0;
		void *nope = CreateInterface( "NOT_A_REAL_INTERFACE", &rc2 );
		check( "CreateInterface(unknown) returns NULL", nope == NULL && rc2 != 0 );
	}

	/* ILuaConVars is a separate interface the engine also requests. */
	{
		int rc3 = 0;
		void *cv = CreateInterface( "LUACONVARS001", &rc3 );
		check( "CreateInterface(LUACONVARS001) returns the convar object", cv != NULL );
	}

	LUA = shared->CreateLuaInterface( 1 /* SERVER */, true );
	check( "CreateLuaInterface(SERVER) returned an interface", LUA != NULL );
	if ( LUA == NULL ) return 1;

	state = LUA->GetState();
	check( "interface GetState() is non-null", state != NULL );
	check( "state->luabase points back at the interface", state->luabase == LUA );

	check( "IsServer() true on the server realm", LUA->IsServer() );
	check( "IsClient() false on the server realm", !LUA->IsClient() );

	loadRc = GMOD_LoadBinaryModule( state, modPath );
	printf( "[host] GMOD_LoadBinaryModule(\"%s\") = %d\n", modPath, loadRc );
	check( "GMOD_LoadBinaryModule reported success (1)", loadRc == 1 );

	/* The module's gmod13_open pushed a string global. */
	{
		const char *marker = getGlobalString( LUA, "opglua_test_marker" );
		printf( "[host] opglua_test_marker = %s\n", marker ? marker : "(nil)" );
		check( "module global opglua_test_marker present",
		       marker != NULL && strcmp( marker, "hello-from-opglua-testmod" ) == 0 );
	}

	/* The module's numeric global. */
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_number" );
	{
		double n = LUA->GetNumber( -1 );
		printf( "[host] opglua_test_number = %g\n", n );
		check( "module global opglua_test_number == 1234.5", n == 1234.5 );
	}
	LUA->Pop( 1 );

	/* The module's boolean global. */
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_flag" );
	check( "module global opglua_test_flag == true", LUA->GetBool( -1 ) );
	LUA->Pop( 1 );

	/* The path argument the loader passed to gmod13_open. */
	{
		const char *arg = getGlobalString( LUA, "opglua_test_arg" );
		printf( "[host] opglua_test_arg = %s\n", arg ? arg : "(nil)" );
		check( "module received the module path argument",
		       arg != NULL && strcmp( arg, modPath ) == 0 );
	}

	/* Type name / pooled string sanity (ILuaBase + ILuaInterface slots). */
	check( "GetTypeName(6) == function", strcmp( LUA->GetTypeName( 6 ), "function" ) == 0 );
	check( "GetPooledString(196) == FireAnimationEvent",
	       strcmp( LUA->GetPooledString( 196 ), "FireAnimationEvent" ) == 0 );

	/* Run a Lua string that reads the module global (RunStringEx path). */
	{
		const char *code = "opglua_roundtrip = opglua_test_marker .. ':' .. opglua_test_number";
		bool ok = LUA->RunStringEx( "@host_chunk", "", code, true, true, true, false );
		check( "RunStringEx executed a chunk", ok );
		const char *rt = getGlobalString( LUA, "opglua_roundtrip" );
		printf( "[host] opglua_roundtrip = %s\n", rt ? rt : "(nil)" );
		check( "chunk saw the module globals",
		       rt != NULL && strcmp( rt, "hello-from-opglua-testmod:1234.5" ) == 0 );
	}

	/* Require -> binary module fallback.  OPGLUA_GAMEDIR points at a tree
	** holding lua/bin/gmsv_<name>_win64.dll.  Clear the module's marker
	** first so a successful Require is observable. */
	{
		LUA->PushNil();
		LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, "opglua_test_marker" );
		LUA->Require( "opglua_reqmod" );
		const char *m2 = getGlobalString( LUA, "opglua_test_marker" );
		printf( "[host] Require(\"opglua_reqmod\") marker = %s\n", m2 ? m2 : "(nil)" );
		check( "Require(name) loaded the binary module",
		       m2 != NULL && strcmp( m2, "hello-from-opglua-testmod" ) == 0 );
	}

	/* Negative paths: the loader return-code contract. */
	{
		int r1 = GMOD_LoadBinaryModule( state, "no_such_module_xyz.dll" );
		printf( "[host] GMOD_LoadBinaryModule(missing file) = %d\n", r1 );
		check( "missing file yields -1", r1 == -1 );
	}
	{
		/* lua51.dll exists but exports no gmod13_open -> -2. */
		int r2 = GMOD_LoadBinaryModule( state, "lua51.dll" );
		printf( "[host] GMOD_LoadBinaryModule(no gmod13_open) = %d\n", r2 );
		check( "missing entry point yields -2", r2 == -2 );
	}

	shared->CloseLuaInterface( LUA );

	printf( "[host] %s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED",
	        g_fail, g_fail == 1 ? "" : "s" );
	return g_fail ? 1 : 0;
}
