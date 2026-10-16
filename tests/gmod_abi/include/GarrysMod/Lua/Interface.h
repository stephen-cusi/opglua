#ifndef GARRYSMOD_LUA_AUTHORITATIVE_INTERFACE_H
#define GARRYSMOD_LUA_AUTHORITATIVE_INTERFACE_H
// Module-facing lua_State shim + LUA_FUNCTION/GMOD_MODULE macros.
//
// Values the reference build uses on win64:
//   offsetof(lua_State, luabase) = 0x78 = 120
//   sizeof(lua_State)            = 0x80 = 128
// This corrects the public GarrysMod/Lua/Interface.h mirror, whose
// "unsigned char _ignore_this_common_lua_header_[92 + 22];" puts luabase at
// offset 114.  The reference value is +28 (120).  A module built against the
// mirror would read the wrong slot for luabase; build against THIS header
// (or simply use offsetof(luabase) == 120).
//
// The reference interface stores its back pointer into the state once at
// boot (its Init slot), so a module reads L->luabase to obtain the ILuaBase.

#ifdef GMOD_USE_ILUAINTERFACE
#include "LuaInterface.h"
namespace GarrysMod { namespace Lua { typedef ILuaInterface ILua; } }
#else
#include "LuaBase.h"
namespace GarrysMod { namespace Lua { typedef ILuaBase ILua; } }
#endif

struct lua_State
{
#if defined( _WIN32 ) && defined( _M_X64 )
    unsigned char _ignore_this_common_lua_header_[92 + 28]; // offset 120
#elif defined( _WIN32 )
    unsigned char _ignore_this_common_lua_header_[48 + 28];  // win32: 76
#else
    unsigned char _ignore_this_common_lua_header_[92 + 28];
#endif

    GarrysMod::Lua::ILua *luabase;
};

#ifndef GMOD
    #ifdef _WIN32
        #define GMOD_DLL_EXPORT extern "C" __declspec( dllexport )
    #else
        #define GMOD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
    #endif

    #define GMOD_MODULE_OPEN()                                  \
        int gmod13_open__Imp( GarrysMod::Lua::ILua *LUA );      \
        GMOD_DLL_EXPORT int gmod13_open( lua_State* L )         \
        {                                                       \
            return gmod13_open__Imp( L->luabase );              \
        }                                                       \
        int gmod13_open__Imp( GarrysMod::Lua::ILua *LUA )

    #define GMOD_MODULE_CLOSE()                                 \
        int gmod13_close__Imp( GarrysMod::Lua::ILua *LUA );     \
        GMOD_DLL_EXPORT int gmod13_close( lua_State* L )        \
        {                                                       \
            return gmod13_close__Imp( L->luabase );             \
        }                                                       \
        int gmod13_close__Imp( GarrysMod::Lua::ILua *LUA )

    #define LUA_FUNCTION( FUNC )                                \
        int FUNC##__Imp( GarrysMod::Lua::ILua *LUA );           \
        int FUNC( lua_State* L )                                \
        {                                                       \
            GarrysMod::Lua::ILua *LUA = L->luabase;             \
            LUA->SetState(L);                                   \
            return FUNC##__Imp( LUA );                          \
        }                                                       \
        int FUNC##__Imp( GarrysMod::Lua::ILua *LUA )

    #define LUA_FUNCTION_STATIC( FUNC )                         \
        static int FUNC##__Imp( GarrysMod::Lua::ILua *LUA );    \
        static int FUNC( lua_State* L )                         \
        {                                                       \
            GarrysMod::Lua::ILua *LUA = L->luabase;             \
            LUA->SetState(L);                                   \
            return FUNC##__Imp( LUA );                          \
        }                                                       \
        static int FUNC##__Imp( GarrysMod::Lua::ILua *LUA )

    #define LUA_FUNCTION_DECLARE( FUNC ) int FUNC( lua_State *L )
    #define LUA_FUNCTION_STATIC_DECLARE( FUNC ) static int FUNC( lua_State *L )
#endif

#endif
