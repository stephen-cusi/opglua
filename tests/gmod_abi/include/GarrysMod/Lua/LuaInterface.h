#ifndef GARRYSMOD_LUA_AUTHORITATIVE_LUAINTERFACE_H
#define GARRYSMOD_LUA_AUTHORITATIVE_LUAINTERFACE_H
// GarrysMod::Lua::ILuaInterface (+CLuaInterface) - virtual order the reference
// win64 build uses.
//
// ILuaInterface : public ILuaBase  adds 76 own virtuals -> slots 55..130.
// CLuaInterface adds one own virtual destructor      -> slot 131.
// Total 55 + 76 + 1 = 132 slots.
//
// WARNING (ABI): the SIX methods at slots 80..85 are declared in the order
// below, which is DIFFERENT from the gmodcommon mirror header.  The Slot
// enum below documents the exact slot of the anchors for cross-checking.
#include <cstddef>
#include <string>

#include "LuaBase.h"

struct lua_Debug;
struct Vector;
class CCommand;
class Color;
class ConVar;
class ConCommand;

namespace Bootil { class Buffer; }

namespace GarrysMod
{
    namespace Lua
    {
        class ILuaGameCallback;
        class ILuaObject;

        class ILuaThreadedCall
        {
        public:
            virtual void Init() = 0;
            virtual void Run( ILuaBase* ) = 0;
        };

        class ILuaInterface : public ILuaBase
        {
        public:
            // slot 55
            virtual bool Init( ILuaGameCallback*, bool ) = 0;
            // slot 56
            virtual void Shutdown() = 0;
            // slot 57
            virtual void Cycle() = 0;
            // slot 58
            virtual ILuaObject* Global() = 0;
            // slot 59
            virtual ILuaObject* GetObject( int index ) = 0;
            // slot 60
            virtual void PushLuaObject( ILuaObject* obj ) = 0;
            // slot 61
            virtual void PushLuaFunction( CFunc func ) = 0;
            // slot 62
            virtual void LuaError( const char* err, int index ) = 0;
            // slot 63
            virtual void TypeError( const char* name, int index ) = 0;
            // slot 64
            virtual void CallInternal( int args, int rets ) = 0;
            // slot 65
            virtual void CallInternalNoReturns( int args ) = 0;
            // slot 66
            virtual bool CallInternalGetBool( int args ) = 0;
            // slot 67
            virtual const char* CallInternalGetString( int args ) = 0;
            // slot 68
            virtual bool CallInternalGet( int args, ILuaObject* obj ) = 0;
            // slot 69
            virtual void NewGlobalTable( const char* name ) = 0;
            // slot 70
            virtual ILuaObject* NewTemporaryObject() = 0;
            // slot 71
            virtual bool isUserData( int index ) = 0;
            // slot 72
            virtual ILuaObject* GetMetaTableObject( const char* name, int type ) = 0;
            // slot 73
            virtual ILuaObject* GetMetaTableObject( int index ) = 0;
            // slot 74
            virtual ILuaObject* GetReturn( int index ) = 0;
            // slot 75
            virtual bool IsServer() = 0;
            // slot 76
            virtual bool IsClient() = 0;
            // slot 77
            virtual bool IsMenu() = 0;
            // slot 78
            virtual void DestroyObject( ILuaObject* obj ) = 0;
            // slot 79
            virtual ILuaObject* CreateObject() = 0;
            // slot 80  <-- reference order starts here (differs from mirror)
            virtual void SetMember( ILuaObject* table, const char* key, ILuaObject* value ) = 0;
            // slot 81
            virtual void SetMember( ILuaObject* table, const char* key ) = 0;
            // slot 82
            virtual void SetMember( ILuaObject* table, float key, ILuaObject* value ) = 0;
            // slot 83
            virtual void SetMember( ILuaObject* table, float key ) = 0;
            // slot 84
            virtual void SetMember( ILuaObject* table, ILuaObject* key, ILuaObject* value ) = 0;
            // slot 85
            virtual ILuaObject* GetNewTable() = 0;
            // slot 86
            virtual void SetType( unsigned char ) = 0;
            // slot 87
            virtual void PushLong( long num ) = 0;
            // slot 88
            virtual int GetFlags( int index ) = 0;
            // slot 89
            virtual bool FindOnObjectsMetaTable( int objIndex, int keyIndex ) = 0;
            // slot 90
            virtual bool FindObjectOnTable( int tableIndex, int keyIndex ) = 0;
            // slot 91
            virtual void SetMemberFast( ILuaObject* table, int keyIndex, int valueIndex ) = 0;
            // slot 92
            virtual bool RunString( const char* filename, const char* path, const char* stringToRun, bool run, bool showErrors ) = 0;
            // slot 93
            virtual bool IsEqual( ILuaObject* objA, ILuaObject* objB ) = 0;
            // slot 94
            virtual void Error( const char* err ) = 0;
            // slot 95
            virtual const char* GetStringOrError( int index ) = 0;
            // slot 96
            virtual bool RunLuaModule( const char* name ) = 0;
            // slot 97
            virtual bool FindAndRunScript( const char* filename, bool run, bool showErrors, const char* stringToRun, bool noReturns ) = 0;
            // slot 98
            virtual void SetPathID( const char* pathID ) = 0;
            // slot 99
            virtual const char* GetPathID() = 0;
            // slot 100
            virtual void ErrorNoHalt( const char* fmt, ... ) = 0;
            // slot 101
            virtual void Msg( const char* fmt, ... ) = 0;
            // slot 102
            virtual void PushPath( const char* path ) = 0;
            // slot 103
            virtual void PopPath() = 0;
            // slot 104
            virtual const char* GetPath() = 0;
            // slot 105
            virtual int GetColor( int index ) = 0;
            // slot 106
            virtual void* PushColor( Color color ) = 0;
            // slot 107
            virtual int GetStack( int level, lua_Debug* dbg ) = 0;
            // slot 108
            virtual int GetInfo( const char* what, lua_Debug* dbg ) = 0;
            // slot 109
            virtual const char* GetLocal( lua_Debug* dbg, int n ) = 0;
            // slot 110
            virtual const char* GetUpvalue( int funcIndex, int n ) = 0;
            // slot 111
            virtual bool RunStringEx( const char* filename, const char* path, const char* stringToRun, bool run, bool printErrors, bool dontPushErrors, bool noReturns ) = 0;
            // slot 112
            virtual size_t GetDataString( int index, const char** str ) = 0;
            // slot 113
            virtual void ErrorFromLua( const char* fmt, ... ) = 0;
            // slot 114
            virtual const char* GetCurrentLocation() = 0;
            // slot 115
            virtual void MsgColour( const Color& col, const char* fmt, ... ) = 0;
            // slot 116
            virtual void GetCurrentFile( std::string& outStr ) = 0;
            // slot 117
            virtual bool CompileString( Bootil::Buffer& dumper, const std::string& stringToCompile ) = 0;
            // slot 118
            virtual bool CallFunctionProtected( int, int, bool ) = 0;
            // slot 119
            virtual void Require( const char* name ) = 0;
            // slot 120
            virtual const char* GetActualTypeName( int type ) = 0;
            // slot 121
            virtual void PreCreateTable( int arrelems, int nonarrelems ) = 0;
            // slot 122
            virtual void PushPooledString( int index ) = 0;
            // slot 123
            virtual const char* GetPooledString( int index ) = 0;
            // slot 124
            virtual int AddThreadedCall( ILuaThreadedCall* call ) = 0;
            // slot 125
            virtual void AppendStackTrace( char*, unsigned int ) = 0;
            // slot 126
            virtual ConVar* CreateConVar( const char* name, const char* defaultValue, const char* helpString, int flags ) = 0;
            // slot 127
            virtual ConCommand* CreateConCommand( const char* name, const char* helpString, int flags, void ( *callback )( const CCommand& ), int ( *completionFunc )( const char*, char ( * )[128] ) ) = 0;
            // slot 128
            virtual const char* CheckStringOpt( int iStackPos, const char* def ) = 0;
            // slot 129
            virtual double CheckNumberOpt( int iStackPos, double def ) = 0;
            // slot 130
            virtual int RegisterMetaTable( const char* name, ILuaObject* tbl ) = 0;
        };

        // The concrete object the reference build ships.  Adds exactly one own
        // virtual (the deleting destructor) after ILuaInterface -> slot 131.
        class CLuaInterface : public ILuaInterface
        {
        public:
            virtual ~CLuaInterface() = default;

            // Offsets inside the concrete object (x64).  Layout only; do not
            // rely on the member names.
            //   +0x000 vtable
            //   +0x008 lua_State* state     (ILuaBase::state)
            //   +0x010 _1 (int, "-1")       +0x018 m_sCurrentPath
            //   +0x020 _2 (int)  +0x024 _3  +0x028 m_iPushedPaths (path stack)
            //   +0x030 m_sLastPath
            //   +0x038 m_pThreadedCalls (std::list head)
            //   +0x048 m_pProtectedFunctionReturns[4]
            //   +0x068 m_pTempObjects[32]
            //   +0x168 m_iRealm (CLIENT 0 / SERVER 1 / MENU 2)
            //   +0x170 m_pGameCallback
            //   +0x178 m_sPathID[32]
            //   +0x198 m_iCurrentTempObject
            //   +0x1a0 m_pGlobal
            //   +0x1a8 m_pStringPool
            //   +0x1b0 m_iMetaTableIDCounter
            //   +0x1b8 m_pMetaTables[255]
        };

        // ---- slot cross-reference (compile-time sanity; not used at runtime) ----
        enum Slot
        {
            SLOT_Top = 0, SLOT_PushNumber = 30, SLOT_GetType = 40,
            SLOT_CheckString = 43, SLOT_CheckNumber = 44, SLOT_SetState = 50,
            SLOT_Init = 55, SLOT_RunString = 92, SLOT_Error = 94,
            SLOT_FindAndRunScript = 97, SLOT_RunStringEx = 111,
            SLOT_ErrorFromLua = 113, SLOT_CheckStringOpt = 128,
            SLOT_GetNewTable = 85, SLOT_LastHeaderSlot = 130, SLOT_Dtor = 131,
            SLOT_COUNT = 132
        };
    }
}
#endif
