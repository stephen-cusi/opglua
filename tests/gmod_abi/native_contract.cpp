#include <cstddef>
#include "GarrysMod/Lua/Interface.h"

static_assert(offsetof(lua_State, luabase) == 120, "Lua interface pointer layout");
static int closes;

LUA_FUNCTION_STATIC(native_echo)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::String);
    LUA->Push(1);
    int reference = LUA->ReferenceCreate();
    LUA->ReferencePush(reference);
    LUA->ReferenceFree(reference);
    LUA->PushNumber(22);
    LUA->PushBool(true);
    return 3;
}

LUA_FUNCTION_STATIC(native_error)
{
    LUA->ThrowError("opglua_expected_native_error");
    return 0;
}

GMOD_MODULE_OPEN()
{
    LUA->CreateTable();
    LUA->PushCFunction(native_echo);
    LUA->SetField(-2, "echo");
    LUA->PushCFunction(native_error);
    LUA->SetField(-2, "fail");
    LUA->Push(-1);
    LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "opglua_native_contract");
    return 1;
}

GMOD_MODULE_CLOSE()
{
    ++closes;
    return 0;
}

extern "C" __declspec(dllexport) int opglua_contract_close_count() { return closes; }
