#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "tier1/convar.h"

static void *slots[48];
static struct { void **vtable; } host = {slots};
static std::map<std::string, void *> variables;
static int notifications;

static int allocate_id(void *) { return 71; }
static void register_variable(void *, void *object)
{
    const char *name = *(const char **)((char *)object + 0x18);
    assert(name);
    variables[name] = object;
    *((char *)object + 0x10) = 1;
}
static void *find_variable(void *, const char *name)
{
    std::map<std::string, void *>::iterator it = variables.find(name);
    return it == variables.end() ? NULL : it->second;
}
static int process_queued(void *) { return 0; }
static void changed(void *, void *, const char *, float) { ++notifications; }
static bool thread_allowed(void *) { return true; }
static void *factory(const char *name, int *)
{
    return !std::strcmp(name, "VEngineCvar007") ? &host : NULL;
}
static void **vtable(void *object) { return *(void ***)object; }

static const CCommand *dispatched_command;
static void command_callback(const CCommand &command) { dispatched_command = &command; }
static int complete_command(const char *, char [COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH]) { return 0; }

static void test_realm_registration(void *shared, bool commands_only)
{
    const unsigned inputs[] = {0, 128, 0xffffffffu, 0x0c00000cu};
    for (unsigned realm = 0; realm != 3; ++realm) {
        void *iface = ((void *(*)(void *, unsigned char, bool))vtable(shared)[4])(shared, (unsigned char)realm, true);
        assert(iface);
        ((void (*)(void *, unsigned char))vtable(iface)[86])(iface, (unsigned char)realm);
        for (unsigned i = 0; i != sizeof(inputs) / sizeof(inputs[0]); ++i) {
            char command_name[80], variable_name[80];
            std::sprintf(command_name, "opglua_contract_command_%u_%u", realm, i);
            std::sprintf(variable_name, "opglua_contract_variable_%u_%u", realm, i);
            unsigned expected = inputs[i] & 0xf3fffff3u;
            if (realm == 1) expected |= 0x08000004u;
            if (realm == 0) expected |= 0x14000008u;
            if (realm == 2) expected &= ~128u;
            unsigned variable_flags = expected;
            if (!commands_only) {
                void *variable = ((void *(*)(void *, const char *, const char *, const char *, int))vtable(iface)[126])(
                    iface, variable_name, "12", "realm test", (int)inputs[i]);
                assert(variable);
                variable_flags = *(unsigned *)((char *)variable + 0x28);
            }
            unsigned command_expected = expected | (realm == 1 ? 0x40000000u : 0u);
            void *command = ((void *(*)(void *, const char *, const char *, int, void *, void *))vtable(iface)[127])(
                iface, command_name, "realm test", (int)inputs[i], (void *)command_callback, (void *)complete_command);
            assert(command);
            unsigned command_flags = *(unsigned *)((char *)command + 0x28);
            if (commands_only)
                std::printf("realm|%u|input|%08x|command|%08x\n", realm, inputs[i], command_flags);
            else
                std::printf("realm|%u|input|%08x|variable|%08x|command|%08x\n", realm, inputs[i], variable_flags, command_flags);
            assert(variable_flags == expected);
            assert(command_flags == command_expected);
            assert(*(void **)((char *)command + 0x30) == (void *)command_callback);
            assert(*(void **)((char *)command + 0x38) == (void *)complete_command);
            assert(((bool (*)(void *))vtable(command)[13])(command));
            // Only the callback identity matters; no CCommand fields are read.
            alignas(8) char argument_storage[1600] = {};
            dispatched_command = NULL;
            ((void (*)(void *, const CCommand &))vtable(command)[14])(command, *(const CCommand *)argument_storage);
            assert(dispatched_command == (const CCommand *)argument_storage);
        }
    }
    std::puts("PASS actual DLL realm flags, command callbacks and argument identity");
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    static_assert(sizeof(ConCommandBase) == 0x30, "Console base layout");
    static_assert(sizeof(ConVar) == 0x90, "Console variable layout");
    slots[9] = (void *)allocate_id;
    slots[10] = (void *)register_variable;
    slots[16] = slots[17] = (void *)find_variable;
    slots[22] = (void *)changed;
    slots[36] = (void *)thread_allowed;
    slots[41] = (void *)process_queued;
    HMODULE module = LoadLibraryA(argv[1]);
    assert(module);
    auto create = (void *(*)(const char *, int *))GetProcAddress(module, "CreateInterface");
    assert(create);
    void *shared = create("LUASHARED003", NULL);
    bool commands_only = argc == 3 && !std::strcmp(argv[2], "commands-only");
    if (!commands_only)
        ((void (*)(void *, void *, bool, void *, void *))vtable(shared)[1])(shared, (void *)factory, false, NULL, NULL);
    if (argc == 3) {
        test_realm_registration(shared, commands_only);
        return 0;
    }
    void *console = create("LUACONVARS001", NULL);
    auto make = (void *(*)(void *, const char *, const char *, const char *, int))vtable(console)[2];
    char borrowed_name[] = "opglua_contract_borrowed";
    char borrowed_default[] = "17";
    char borrowed_help[] = "borrowed help";
    void *borrowed = make(console, borrowed_name, borrowed_default, borrowed_help, 128);
    char borrowed_command_name[] = "opglua_contract_borrowed_command";
    auto make_command = (void *(*)(void *, const char *, const char *, int, void *, void *))vtable(console)[3];
    void *borrowed_command = make_command(console, borrowed_command_name, borrowed_help, 0,
                                          (void *)command_callback, (void *)complete_command);
    std::memset(borrowed_name, 'x', sizeof(borrowed_name) - 1);
    std::memset(borrowed_default, 'x', sizeof(borrowed_default) - 1);
    std::memset(borrowed_help, 'x', sizeof(borrowed_help) - 1);
    std::memset(borrowed_command_name, 'x', sizeof(borrowed_command_name) - 1);
    assert(!std::strcmp(*(const char **)((char *)borrowed + 0x18), "opglua_contract_borrowed"));
    assert(!std::strcmp(*(const char **)((char *)borrowed + 0x20), "borrowed help"));
    assert(!std::strcmp(*(const char **)((char *)borrowed + 0x40), "17"));
    assert(!std::strcmp(*(const char **)((char *)borrowed_command + 0x18), "opglua_contract_borrowed_command"));
    assert(!std::strcmp(*(const char **)((char *)borrowed_command + 0x20), "borrowed help"));
    std::puts("PASS console strings survive caller buffer reuse");
    void *object = make(console, "opglua_contract_value", "12", "test value", 128);
    assert(object && variables["opglua_contract_value"] == object);
    assert(*(void **)((char *)object + 0x38) == object);
    void *secondary = (char *)object + 0x30;
    void **main_table = vtable(object), **sub_table = vtable(secondary);
    assert(!((bool (*)(void *))main_table[1])(object));
    assert(((bool (*)(void *))main_table[8])(object));
    assert(((float (*)(void *))main_table[12])(object) == 12.0f);
    assert(((int (*)(void *))main_table[13])(object) == 12);
    assert(!std::strcmp(((const char *(*)(void *))sub_table[4])(secondary), "opglua_contract_value"));
    assert(!std::strcmp(((const char *(*)(void *))sub_table[5])(secondary), "opglua_contract_value"));
    assert(((bool (*)(void *, int))sub_table[6])(secondary, 128));
    assert(!((bool (*)(void *, int))sub_table[6])(secondary, 256));
    assert(((int (*)(void *))sub_table[7])(secondary) == 0);
    ((void (*)(void *, int))sub_table[1])(secondary, 27);
    assert(((int (*)(void *))main_table[13])(object) == 27);
    ((void (*)(void *, float))sub_table[2])(secondary, 3.5f);
    assert(((float (*)(void *))main_table[12])(object) == 3.5f);
    ((void (*)(void *, const char *))sub_table[3])(secondary, "44");
    assert(((int (*)(void *))main_table[13])(object) == 44);
    assert(!std::strcmp(*(const char **)((char *)object + 0x48), "44"));
    ((void (*)(void *, unsigned))sub_table[0])(secondary, 0x04030201u);
    assert(((int (*)(void *))main_table[13])(object) == 0x04030201);
    assert(notifications >= 4);
    assert(make(console, "opglua_contract_value", "99", "duplicate", 128) == object);
    ((void (*)(void *))vtable(console)[4])(console);
    assert(((int (*)(void *))main_table[13])(object) == 0x04030201);
    std::puts("PASS actual DLL console registration, both method tables, value types, callback dispatch, duplicate lookup and managed lifetime");
    return 0;
}
