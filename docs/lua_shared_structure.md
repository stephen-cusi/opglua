# lua_shared structure reference

A complete structural map of the shipping Garry's Mod `lua_shared.dll` (win64),
and how far this implementation follows it. Everything here was established from
the reference binary's runtime type information, its virtual tables and the
bodies of the functions those tables point at; the raw evidence lives in the
local forensics archive and addresses are deliberately not repeated here.

Object field offsets (`+0x170`) are ABI facts, not code addresses.

---

## 1. Object inventory

| Object | Slots | Role |
|---|---|---|
| `CLuaShared` | 19 | the object `CreateInterface("LUASHARED003")` returns; owns one interface per realm |
| `CLuaInterface` | 132 | one per realm; `ILuaBase` (0..54) + `ILuaInterface` (55..130) + own destructor (131) |
| `CLuaConVars` | 8 | `CreateInterface("LUACONVARS001")`; builds ConVar/ConCommand objects for scripts |
| `ILuaGameCallback` | 7 | supplied by the host; how the runtime asks the game for objects and messages |
| `ILuaObject` | 40+ | host-owned value handle; **never** inspected by field, only driven through its vtable |
| `ILuaThreadedCall` | 3 | work item queued with `AddThreadedCall` and drained by `Cycle` |

All three `CreateInterface` answers are process-wide singletons: the engine
caches the first answer and hands the same pointer to every module.

---

## 2. `CLuaShared` (19 slots)

```
+0x00  vtable
+0x08  engine interface factory (transient; not stored in the reference)
+0x10  dedicated/magic flag
+0x18  engine file system
+0x78  ILuaInterface* realm[3]   ({CLIENT=0, SERVER=1, MENU=2})
```

| slot | method | contract |
|---|---|---|
| 0 | destructor | |
| 1 | `Init(factory, dedicated, ctx, get)` | pulls `VEngineCvar007` and `VFileSystem022`, initialises the ConVar module, records the flag; **state creation does not happen here** |
| 2 | `Shutdown` | |
| 3 | `DumpStats` | prints file-timestamp timings |
| 4 | `CreateLuaInterface(realm, bool)` | allocates + constructs, stores into `realm[realm]`; **does not create a `lua_State`** |
| 5 | `CloseLuaInterface` | `Shutdown()` then destructor, clears the realm slot, and when both client and server are gone calls the ConVar module's `DestroyManaged` |
| 6 | `GetLuaInterface(realm)` | plain read of `realm[realm]`; **not** create-on-demand |
| 7 | `LoadFile(name, pathID, fromDatatable, fromFile)` | returns a cached `LuaFile*` or NULL |
| 8 | `GetCache(name)` | pure cache lookup |
| 9 | `MountLua(pathID)` | mounts `lua`; also `gamemodes` unless the id is the menu realm |
| 10 | `MountLuaAdd(root, pathID)` | resolves the root, adds the search path, marks the id request-only |
| 11 | `UnMountLua(pathID)` | |
| 12 | `SetFileContents` | empty in the shipping build |
| 13 | `SetLuaFindHook` | |
| 14 | `FindScripts(path, pathID, out)` | directory walk; results are base names + folder flag |
| 15 | `GetStackTraces` | walks all three realms, labelled Client/Server/MenuSystem |
| 16 | `InvalidateCache` | |
| 17 | `EmptyCache` | |
| 18 | `ScriptExists` | accepts a directory |

**Realm indices are part of the ABI.** The realm table sits at `+0x78` with a
stride of 8, and both slot 4 and slot 6 address it. A caller reading the object
directly and a caller going through the table must agree, so this member's
placement is asserted at compile time in this tree.

---

## 3. `CLuaInterface` (132 slots)

Slot = byte offset / 8. `ILuaBase` supplies 0..54, `ILuaInterface` 55..130, and
the class adds its own deleting destructor at 131.

Essential base slots:

| slot | byte | method | note |
|---|---|---|---|
| 0 | 0x000 | Top | |
| 1 | 0x008 | Push | |
| 2 | 0x010 | Pop | errors on over-pop |
| 3 | 0x018 | GetTable | |
| 6 | 0x030 | CreateTable | |
| 10 | 0x050 | Call | |
| 11 | 0x058 | PCall | |
| 17 | 0x088 | NewUserdata | deprecated, still occupies its slot |
| 23 | 0x0b8 | GetString | returns NULL on failure |
| 29 | 0x0e8 | PushString | |
| 30 | 0x0f0 | PushNumber | |
| 35 | 0x118 | ReferenceCreate | |
| 36 | 0x120 | ReferenceFree | |
| 37 | 0x128 | ReferencePush | |
| 39 | 0x138 | IsType | |
| 40 | 0x140 | GetType | type plus custom user-type byte |
| 41 | 0x148 | GetTypeName | |
| 43 | 0x158 | CheckString | throwing |
| 44 | 0x160 | CheckNumber | throwing |
| 48 | 0x180 | PushAngle | |
| 49 | 0x188 | PushVector | |
| 50 | 0x190 | SetState | writes `state` |
| 51 | 0x198 | CreateMetaTable | |
| 52 | 0x1a0 | PushMetaTable | |

Interface slots:

| slot | byte | method | note |
|---|---|---|---|
| 55 | 0x1b8 | Init | the whole boot sequence |
| 56 | 0x1c0 | Shutdown | |
| 57 | 0x1c8 | Cycle | per frame: clear returns, stack check, drain threaded calls |
| 58 | 0x1d0 | Global | returns the global object |
| 59 | 0x1d8 | GetObject | temp object bound to a stack index |
| 64 | 0x200 | CallInternal | |
| 65 | 0x208 | CallInternalNoReturns | |
| 66 | 0x210 | CallInternalGetBool | |
| 67 | 0x218 | CallInternalGetString | |
| 68 | 0x220 | CallInternalGet | |
| 70 | 0x230 | NewTemporaryObject | 32-entry ring |
| 72/73 | 0x240/0x248 | GetMetaTableObject | by name / by index |
| 74 | 0x250 | GetReturn | |
| 75/76/77 | 0x258/0x260/0x268 | IsServer/IsClient/IsMenu | realm byte test |
| 78/79 | 0x270/0x278 | DestroyObject/CreateObject | through the game callback |
| 80..85 | 0x280..0x2a8 | SetMember overloads, GetNewTable | **shipping order differs from the public header mirror** |
| 92 | 0x2e0 | RunString | forwarder to 111 |
| 96 | 0x300 | RunLuaModule | |
| 97 | 0x308 | FindAndRunScript | |
| 98/99 | 0x310/0x318 | SetPathID/GetPathID | |
| 102..104 | 0x330..0x340 | PushPath/PopPath/GetPath | |
| 111 | 0x378 | RunStringEx | core compile + run |
| 118 | 0x3b0 | CallFunctionProtected | |
| 119 | 0x3b8 | Require | |
| 122/123 | 0x3d0/0x3d8 | PushPooledString/GetPooledString | |
| 124 | 0x3e0 | AddThreadedCall | |
| 126/127 | 0x3f0/0x3f8 | CreateConVar/CreateConCommand | |
| 130 | 0x410 | RegisterMetaTable | |
| 131 | 0x418 | deleting destructor | |

### Shipping `SetMember` order (slots 80..85)

```
80  SetMember(table, const char* key, ILuaObject* value)
81  SetMember(table, const char* key)
82  SetMember(table, float key, ILuaObject* value)
83  SetMember(table, float key)
84  SetMember(table, ILuaObject* key, ILuaObject* value)
85  ILuaObject* GetNewTable()
```

The public `LuaInterface.h` mirror shipped by `garrysmod_common` declares this
same set in a different order, with `GetNewTable` third. A module built against
that mirror calls the wrong functions on six consecutive slots. This tree
defines its table by hand in the shipping order above; the mirror is left
untouched because it is an upstream project, and `tests/gmod_abi` carries a
corrected header for tests.

---

## 4. `CLuaInterface` memory layout

Derived from which slots read or write each offset. `state` is the only field
touched by the whole base table, which is why slot 0..54 wrappers can be thin.

```
+0x008  lua_State*            state
+0x010  int                   registry ref of the error handler
+0x018  char*                 path stack buffer      (CUtlMemory)
+0x020  uint                  path stack capacity
+0x024  int                   path stack grow size
+0x028  int                   path stack depth
+0x030  char*                 path stack elements
+0x038  list head             pending host calls
+0x040  int                   pending call count (max marker 0x0aaaaaaaaaaaaaaa)
+0x048  ILuaObject* [4]       return slots
+0x068  ILuaObject* [32]      temporary object ring
+0x168  unsigned char         realm (0 client, 1 server, 2 menu)
+0x170  ILuaGameCallback*     host callback
+0x178  char [32]             path id
+0x198  int                   temporary ring cursor
+0x1a0  ILuaObject*           global table object
+0x1a8  ILuaObject*           pooled-string object
+0x1b0  int                   metatable object count (44)
+0x1b8  ILuaObject* [44]      metatable objects, indexed by type
```

Each path stack element is a full 260-byte buffer, and the stack grows; a fixed
array is not equivalent because `PushPath` may be nested deeper than expected.

`PushPath` canonicalises the path through the file system (own-table path
helper), converts separators, and stores the result; `GetPath` returns the top
entry; `PopPath` guards against over-popping.

---

## 5. `ILuaGameCallback` (7 slots)

```
0  ILuaObject* CreateLuaObject()                -- no arguments; the callback is already bound to its realm
1  void        DestroyLuaObject(ILuaObject*)
2  void        ErrorPrint(const char*, bool)
3  void        Msg(const char*, bool)
4  void        MsgColour(const char*, const Color&)
5  void        LuaError(const CLuaError*)
6  void        InterfaceCreated(ILuaInterface*)
```

Consequence for a reimplementation: `CreateObject` must call callback slot 0
with **only** the callback as `this` — the callback already knows its realm
because `Init` called slot 6 with the interface. Passing a state or an interface
into the object layer is wrong.

---

## 6. `ILuaObject` (host-supplied)

The reference never reads object fields; it only calls these vtable slots:

```
+0x008  SetFromStack(int)          bind the object to a stack slot
+0x010  UnReference()              recycle / release
+0x018  GetType() -> int
+0x080  SetMember(const char*, const char*)
+0x108  Push()                     push self on the current stack
+0x110  isNil() -> bool
+0x118  isTable() -> bool
+0x1a8  SetMember(const char*, double)
```

The public `LuaObject.h` mirror is stale for this layout (it puts `Push` at
0xC0); the byte offsets above are the contract.

---

## 7. `ILuaThreadedCall` (3 slots)

```
+0x00  Init()
+0x08  query: returns non-zero when the call is ready to run
+0x10  Run(ILuaInterface*)
```

`Cycle` walks the queue, runs the entries whose query answered non-zero, and
drops exactly those entries; entries that answered zero stay queued. The public
header mirror declares only `Init` and `Run` (2 slots), which is one short.

`AddThreadedCall` returns the queue count and refuses when the queue is at its
0x0aaaaaaaaaaaaaaa marker.

---

## 8. Boot sequence

```
engine: LoadLibrary("lua_shared.dll"); Sys_GetFactory
engine: CreateInterface("LUASHARED003")            -> CLuaShared singleton
engine: CreateInterface("LUACONVARS001")           -> CLuaConVars singleton
engine: ILuaShared::Init(factory, dedicated, ctx, get)
engine: GetLuaInterface(MENU)   -> NULL
engine: GetLuaInterface(CLIENT) -> NULL
engine: CreateLuaInterface(MENU)  -> allocated, stored; no state yet
engine: GetLuaInterface(MENU)   -> the new interface
engine: ILuaInterface::Init(callback, false)
        ... same for CLIENT then SERVER when the realm is needed
```

`ILuaInterface::Init` itself does, in order:

1. store the callback at `+0x170`
2. create the global table object through callback slot 0
3. clear the return slots, the temporary ring, and the metatable table
4. `luaL_newstate`; on failure report and return false
5. `SetState(L)` and write the back pointer at `L+0x78`
6. install the panic handler and keep a registry reference to the error handler
7. build the callback-name table (317 names, index 0 = `AcceptInput`) and keep
    it as the pooled-string object at `+0x1a8`
8. **call callback slot 6 `InterfaceCreated(this)`** — this happens *before*
    the libraries exist, because the host starts pulling in scripts from this
    callback
9. publish `VERSION` (1234) and `BRANCH` ("unknown") into the globals
10. `luaL_openlibs`, with `io` removed afterwards (the reference never exposes
    it)
11. strip `debug.setlocal`, `setupvalue`, `upvalueid`, `upvaluejoin`

The ordering of step 8 is load-bearing: a host that loads script from that
callback sees a state without standard libraries, which is what the reference
gives it.

---

## 9. Script resolution

`FindAndRunScript(name, run, showErrors, marker, noReturns)`:

1. reject anything that is not a `.lua` name (`.lua` appended when the name has
   no extension, any other extension refused)
2. ask `CLuaShared` slot 7 `LoadFile(name, pathID, false, isClient)` — **script
   bytes come through `LoadFile`, not through a private disk walk**, so an
   `include` made by a running script reaches the same cache entry
3. on NULL report "File not found or is empty"
4. on success write the marker into `LuaFile::source` (`+0x28`) and bump the
   per-realm counters (`+0x90` server, `+0x94` client)
5. run the recorded bytes through slot 111, using the **cache entry's own name**
   as the chunk name (with a leading `!` stripped), `PushPath`/`PopPath` around
   it

`RunStringEx(name, path, body, run, showErrors, dontPushErrors, noReturns)`:

- a null or empty body is refused with "Not running script <name> - it's too
  short."; a body beginning with the bytecode escape is refused with
  "Cannot run byte code!"
- the chunk name is `"@" + path + name` with one leading `@` normalised
- a compile failure reports through the game callback with the realm tag and
  optionally pushes the message; a runtime failure goes through the
  protected-call slot and then the same path
- `SCRIPTNAME` and `SCRIPTPATH` are cleared before the chunk runs

`LuaFile` layout (0x98):

```
+0x00  int          time (file timestamp)
+0x08  std::string  name
+0x28  std::string  source
+0x48  std::string  contents
+0x68  byte[32]     compressed copy
+0x88  byte[8]      reserved
+0x90  uint         times loaded server-side
+0x94  uint         times loaded client-side
```

`Require(name)` uses `_MODULES` and an `isNil` test on the looked-up entry:
a missing entry means "not loaded"; the module file is
`lua/includes/modules/<name>.lua` and the result is stored under `_MODULES[name]`.
Modules must therefore execute **once per realm** — double execution registers
every hook twice.

---

## 10. What the LuaJIT side changes

The interpreter is stock LuaJIT 2.1 (beta 3 line, GC64) with:

- GLua lexer: `continue` (token inserted, all later tokens shifted),
  `&&`/`||`/`!`/`!=` aliases, `//` and non-nesting `/* */` comments
- `table.insert` returns the inserted position
- `luaL_newmetatable` carries `MetaName`/`MetaID`; `luaL_newmetatable_type`
- `lua_State` grows to 0x80 with the interface back pointer at 0x78
- a GLua-only error message for `continue` outside a loop

Nothing else in the runtime differs from upstream, so the port keeps LuaJIT
stock wherever the reference does not add behaviour.

---

## 11. Implementation status

Aligned and verified against the reference:

- singleton factories, realm table at `+0x78`, `Init`/`Cycle`/`GetLuaInterface`
  contracts, boot steps 1..12
- `ILuaShared` file slots with a real timestamp-validated `LuaFile` cache fed by
  the engine file system, with the search-path mounting trio
- `FindAndRunScript` reading through `LoadFile` (engine file system first)
- `CallFunctionProtected` running with the interface's error handler
- `CallInternal*` preparing return objects before the call and binding results
- ConVar/ConCommand objects built against the engine's layout and registered
  with its cvar system

Known gaps, in the order they matter:

1. **Threaded calls**: the queue is drained, but `ILuaThreadedCall` is a
   host-typed object with an unverified third slot; the query/run split is
   reconstructed from the drain loop and needs a live check.
2. **C-side libraries**: `file`, `sql`, `net`, `util`, `draw` and friends do not
   exist in this DLL; the reference implements them in the same module
   (`gmod_*` registrations), so scripts that need them fail as soon as they run.
3. **`ILuaObject` usage**: objects are created through the callback and driven
   through the vtable, but a few paths still keep their own handle type, which
   is only safe as long as the host's objects and ours are never mixed.
4. Field offsets past `+0x1b8` (metatable table end) are not yet mapped; the
   reference's path stack grows, ours is fixed-depth.
