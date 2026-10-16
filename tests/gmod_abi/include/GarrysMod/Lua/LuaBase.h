#ifndef GARRYSMOD_LUA_AUTHORITATIVE_LUABASE_H
#define GARRYSMOD_LUA_AUTHORITATIVE_LUABASE_H
// GarrysMod::Lua::ILuaBase - virtual order the reference win64 build uses.
// Virtual order IS the ABI: binary modules link against fixed slot numbers,
// so these declarations may NOT be reordered.  Slots 0..54 (55 methods); the
// anchors are PushNumber, GetType, CheckString and CheckNumber.
#include <cstdarg>
#include <cstdlib>

#include "Types.h"

struct lua_State;
struct lua_Debug;
struct Vector;
struct QAngle;

namespace GarrysMod
{
    namespace Lua
    {
        extern "C"
        {
            void lua_getfenv( lua_State *L, int idx );
            int lua_setfenv( lua_State *L, int idx );
            const char *lua_pushvfstring( lua_State *L, const char *fmt, va_list argp );
            int lua_error( lua_State *L );
            int luaL_typerror( lua_State *L, int narg, const char *tname );
            const void *lua_topointer( lua_State *L, int idx );
            int luaL_callmeta( lua_State *L, int idx, const char *e );
            int lua_getstack( lua_State *L, int level, lua_Debug *ar );
            int lua_getinfo( lua_State *L, const char *what, lua_Debug *ar );
        }

        typedef int ( *CFunc )( lua_State* L );

        // For use with ILuaBase::PushSpecial
        enum
        {
            SPECIAL_GLOB,       // Global table
            SPECIAL_ENV,        // Environment table
            SPECIAL_REG,        // Registry table
        };

        // Use with ILuaBase::GetField/SetField etc. instead of pushing a table
        enum
        {
            INDEX_GLOBAL = -10002,  // Global table
            INDEX_ENVIRONMENT,      // Environment table
            INDEX_REGISTRY,         // Registry table
        };

        class ILuaBase
        {
        public:
            struct UserData
            {
                void*         data;
                unsigned char type;
            };

            // slot 0
            virtual int         Top( void ) = 0;
            // slot 1
            virtual void        Push( int iStackPos ) = 0;
            // slot 2
            virtual void        Pop( int iAmt = 1 ) = 0;
            // slot 3
            virtual void        GetTable( int iStackPos ) = 0;
            // slot 4
            virtual void        GetField( int iStackPos, const char* strName ) = 0;
            // slot 5
            virtual void        SetField( int iStackPos, const char* strName ) = 0;
            // slot 6
            virtual void        CreateTable() = 0;
            // slot 7
            virtual void        SetTable( int iStackPos ) = 0;
            // slot 8
            virtual void        SetMetaTable( int iStackPos ) = 0;
            // slot 9
            virtual bool        GetMetaTable( int i ) = 0;
            // slot 10
            virtual void        Call( int iArgs, int iResults ) = 0;
            // slot 11
            virtual int         PCall( int iArgs, int iResults, int iErrorFunc ) = 0;
            // slot 12
            virtual int         Equal( int iA, int iB ) = 0;
            // slot 13
            virtual int         RawEqual( int iA, int iB ) = 0;
            // slot 14
            virtual void        Insert( int iStackPos ) = 0;
            // slot 15
            virtual void        Remove( int iStackPos ) = 0;
            // slot 16
            virtual int         Next( int iStackPos ) = 0;
            // slot 17 (deprecated, still occupies the slot)
            virtual void*       NewUserdata( unsigned int iSize ) = 0;
            // slot 18
            [[noreturn]] virtual void ThrowError( const char* strError ) = 0;
            // slot 19
            virtual void        CheckType( int iStackPos, int iType ) = 0;
            // slot 20
            [[noreturn]] virtual void ArgError( int iArgNum, const char* strMessage ) = 0;
            // slot 21
            virtual void        RawGet( int iStackPos ) = 0;
            // slot 22
            virtual void        RawSet( int iStackPos ) = 0;
            // slot 23
            virtual const char* GetString( int iStackPos = -1, unsigned int* iOutLen = nullptr ) = 0;
            // slot 24
            virtual double      GetNumber( int iStackPos = -1 ) = 0;
            // slot 25
            virtual bool        GetBool( int iStackPos = -1 ) = 0;
            // slot 26
            virtual CFunc       GetCFunction( int iStackPos = -1 ) = 0;
            // slot 27 (deprecated, still occupies the slot)
            virtual void*       GetUserdata( int iStackPos = -1 ) = 0;
            // slot 28
            virtual void        PushNil() = 0;
            // slot 29
            virtual void        PushString( const char* val, unsigned int iLen = 0 ) = 0;
            // slot 30
            virtual void        PushNumber( double val ) = 0;
            // slot 31
            virtual void        PushBool( bool val ) = 0;
            // slot 32
            virtual void        PushCFunction( CFunc val ) = 0;
            // slot 33
            virtual void        PushCClosure( CFunc val, int iVars ) = 0;
            // slot 34 (deprecated, still occupies the slot)
            virtual void        PushUserdata( void* ) = 0;
            // slot 35
            virtual int         ReferenceCreate() = 0;
            // slot 36
            virtual void        ReferenceFree( int i ) = 0;
            // slot 37
            virtual void        ReferencePush( int i ) = 0;
            // slot 38
            virtual void        PushSpecial( int iType ) = 0;
            // slot 39
            virtual bool        IsType( int iStackPos, int iType ) = 0;
            // slot 40
            virtual int         GetType( int iStackPos ) = 0;
            // slot 41
            virtual const char* GetTypeName( int iType ) = 0;
            // slot 42 (deprecated, still occupies the slot)
            virtual void        CreateMetaTableType( const char* strName, int iType ) = 0;
            // slot 43
            virtual const char* CheckString( int iStackPos = -1 ) = 0;
            // slot 44
            virtual double      CheckNumber( int iStackPos = -1 ) = 0;
            // slot 45
            virtual int         ObjLen( int iStackPos = -1 ) = 0;
            // slot 46
            virtual const QAngle& GetAngle( int iStackPos = -1 ) = 0;
            // slot 47
            virtual const Vector& GetVector( int iStackPos = -1 ) = 0;
            // slot 48
            virtual void        PushAngle( const QAngle& val ) = 0;
            // slot 49
            virtual void        PushVector( const Vector& val ) = 0;
            // slot 50
            virtual void        SetState( lua_State* L ) = 0;
            // slot 51
            virtual int         CreateMetaTable( const char* strName ) = 0;
            // slot 52
            virtual bool        PushMetaTable( int iType ) = 0;
            // slot 53
            virtual void        PushUserType( void* data, int iType ) = 0;
            // slot 54
            virtual void        SetUserType( int iStackPos, void* data ) = 0;

            // --- non-virtual helpers (not part of the ABI) ---
            template <class T>
            T* GetUserType( int iStackPos, int iType )
            {
                UserData* ud = static_cast<UserData*>( GetUserdata( iStackPos ) );
                if ( ud == nullptr || ud->data == nullptr || ud->type != iType )
                    return nullptr;
                return static_cast<T*>( ud->data );
            }

            lua_State* GetState() const { return state; }
            void GetFEnv( int i ) { lua_getfenv( state, i ); }
            int SetFEnv( int i ) { return lua_setfenv( state, i ); }
            [[noreturn]] void Error() { lua_error( state ); std::abort(); }
            const void* GetPointer( int i ) { return lua_topointer( state, i ); }
            int CallMeta( int i, const char* e ) { return luaL_callmeta( state, i, e ); }
            static int GetUpvalueIndex( int iPos ) { return static_cast<int>( INDEX_GLOBAL ) - iPos; }
            int GetStack( int level, lua_Debug* ar ) { return lua_getstack( state, level, ar ); }
            int GetInfo( const char* what, lua_Debug* ar ) { return lua_getinfo( state, what, ar ); }

        private:
            lua_State *state;
        };
    }
}
#endif
