# lua_shared structure reference

What the shipping Garry's Mod `lua_shared.dll` (win64) contains, and how far
this implementation matches it. Facts below come from the reference binary's
runtime type information, virtual tables and function bodies; the addresses
live in the local forensics archive (`D:\tmp\dec\lua_shared.dll\`) and are
deliberately not repeated in this repository.

## Object model

```
game / binary module
        |
        | CreateInterface("LUASHARED003")   (factory returns a singleton)
        v
   CLuaShared  ---------------------------+-------------------+
   vtable, 19 slots                       |                   |
        |                                 |                   |
        | slot 4 CreateLuaInterface(realm) | slot 6 GetLuaInterface(realm)
        v                                 v                   v
   CLuaInterface  (one per realm)   ifaces[0] client   ifaces[2] menu
   132-slot vtable                   ifaces[1] server
        |
        | slot 55 Init(gameCallback, dedicated)
        v
   lua_State (LuaJIT 2.1, GC64) ---- L+0x78 -> ILuaBase* (= the CLuaInterface)
```

`CreateInterface("LUACONVARS001")` returns the `CLuaConVars` singleton (8
slots) used to build ConVar/ConCommand objects for scripts.

## CLuaShared (19 virtual slots)

| slot | method | notes |
|---|---|---|
| 0 | destructor | |
| 1 | Init(factory, dedicated, context, get) | binds the engine cvar system and file system |
| 2 | Shutdown | |
| 3 | DumpStats | prints file-timestamp timings |
| 4 | CreateLuaInterface(realm, unk) | allocates; Init follows |
| 5 | CloseLuaInterface | |
| 6 | GetLuaInterface(realm) | plain array read |
| 7 | LoadFile | returns a cached `LuaFile*` |
| 8 | GetCache | pure cache lookup |
| 9 | MountLua(pathID) | mounts `lua`, plus `gamemodes` unless the realm is the menu |
| 10 | MountLuaAdd(root, pathID) | resolves the root, then adds the search path |
| 11 | UnMountLua(pathID) | |
| 12 | SetFileContents | client file transfer |
| 13 | SetLuaFindHook | |
| 14 | FindScripts | directory walk, `{fileName, isFolder}` results |
| 15 | GetStackTraces | |
| 16 | InvalidateCache | |
| 17 | EmptyCache | |
| 18 | ScriptExists | |

The realm table is a fixed array inside the object at `+0x78`, one pointer per
realm, and *both* slot 4 and slot 6 address that array. A caller that reads the
object directly and a caller going through the table must see the same answer,
so the placement is part of the contract (`offsetof` is asserted in the
platform layer).

## CLuaInterface (132 virtual slots)

`ILuaBase` contributes slots 0..54, `ILuaInterface` slots 55..130, and the
class adds its own deleting destructor at 131.

Base block worth memorising (slot = byte offset / 8):

| slot | byte | method |
|---|---|---|
| 0 | 0x00 | Top |
| 1 | 0x08 | Push |
| 2 | 0x10 | Pop |
| 3 | 0x18 | GetTable |
| 10 | 0x50 | Call |
| 11 | 0x58 | PCall |
| 17 | 0x88 | NewUserdata |
| 23 | 0xb0 | GetString |
| 29 | 0xe8 | PushString |
| 30 | 0xf0 | PushNumber |
| 35 | 0x118 | ReferenceCreate |
| 36 | 0x120 | ReferenceFree |
| 37 | 0x128 | ReferencePush |
| 40 | 0x140 | GetType |
| 43 | 0x158 | CheckString |
| 44 | 0x160 | CheckNumber |
| 50 | 0x190 | SetState |
| 51 | 0x198 | CreateMetaTable |
| 55 | 0x1b8 | Init |
| 57 | 0x1c8 | Cycle |
| 58 | 0x1d0 | Global |
| 79 | 0x278 | CreateObject |
| 92 | 0x2e0 | RunString |
| 96 | 0x300 | RunLuaModule |
| 97 | 0x308 | FindAndRunScript |
| 111 | 0x378 | RunStringEx |
| 122 | 0x3d0 | PushPooledString |
| 123 | 0x3d8 | GetPooledString |
| 124 | 0x3e0 | AddThreadedCall |
| 126 | 0x3f0 | CreateConVar |
| 127 | 0x3f8 | CreateConCommand |
| 131 | 0x418 | ~CLuaInterface |

### Init, step by step

1. store the game callback, ask the object factory for the global table object
2. clear the return-slot block and the per-type metatable object table
   (44 entries)
3. `luaL_newstate`, install it through `SetState`, write the back pointer at
   `L+0x78`
4. install the panic handler; push the error reporter and keep its reference
5. FPU precision self-check (a bad x87 state is warned about, not fatal)
6. publish `VERSION` (1234) and `BRANCH` ("unknown") into the globals
7. load the standard libraries and open the base gamemode environment
8. build the 317-entry callback-name table (index 1 = `AcceptInput`) and keep
   it as the string-pool object for `PushPooledString`
9. strip `debug.setlocal`, `setupvalue`, `upvalueid`, `upvaluejoin`
10. call back into the game (`InterfaceCreated`)

Steps 6 and 9 are easy to miss and are now implemented; step 8 exists in this
tree as a flat table in C, which answers `GetPooledString` identically.

### Per-frame work

`Cycle` (slot 57) clears the four return slots and pumps the threaded-call
queue. The queue is a list of host objects handed to `AddThreadedCall`
(slot 124); the client calls it exactly once per map load, so an implementation
that drops the queue loses those calls silently. This is a known gap.

## lua_State

* GC64 build: stock core ends at 0x60; Garry's Mod adds a tail and the
  interface back pointer, for `sizeof = 0x80`.
* `offsetof(lua_State, luabase) = 0x78` — a coroutine state must inherit the
  parent's pointer or every engine callback made from a coroutine reads
  garbage.

## Script loading path

The engine does **not** drive script loading through `ILuaShared::LoadFile`
in practice; it calls `CLuaInterface::FindAndRunScript` (slot 97), which is why
that slot must consult the engine file system itself. `LoadFile`, `GetCache`,
`FindScripts` and `ScriptExists` still matter for gamemode discovery and for
`include`-style lookups.

Resolution order in this implementation: engine file system (with the path id,
then without) -> local disk walk over the mounted roots -> miss.

## ConVar / ConCommand

`CLuaConVars` builds real `ConVar`/`ConCommand` objects that self-register with
the engine's cvar system. For that to work the ConVar block must match the
engine's layout exactly, including the change-callback vector at the tail, and
the virtual order must match (see `public/tier1/convar.h` on the engine
`opglua` branch).
