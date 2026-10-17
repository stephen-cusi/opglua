# opglua

An open-source, from-scratch implementation of Garry's Mod's `lua_shared.dll` —
the shared library that hosts the game's Lua state (GLua), loads gamemodes,
addons and scripts, and exposes the binary-module API to `gm_*` modules.

It links LuaJIT 2.1 (GC64) with a platform layer that implements the
`ILuaShared` / `ILuaInterface` / `ILuaConVars` interfaces and the C exports the
engine and binary modules bind to, with the goal of being a drop-in,
source-buildable replacement: no patches to game content, no patches to the
engine.

## GLua language layer

The `glua` branch carries the language and library layer on top of the
upstream 2.1 line (based on the 2.1.0-beta3 release commit):

- `continue` statement: reserved word, works in numeric for / generic for /
  while / repeat, resolves to each loop's continuation point, propagates
  through nested scopes like `break`, and has its own
  "no loop to continue" error.
- C-style operators: `&&` `||` `!` `!=` map onto the logical/equality tokens.
- C-style comments: `//` line comments and non-nesting `/* */` block comments.
- `table.insert` returns the inserted position (interpreter and JIT recorded
  paths agree).
- Metatables created through `luaL_newmetatable` carry `MetaName` (the type
  name string) and `MetaID` (numeric type id) fields; the new
  `luaL_newmetatable_type` API registers a metatable with an explicit id.
  Together with a Lua-level `type()` override this lets userdata report by
  type name (e.g. "Entity") instead of "userdata".
- `lua_resume_real` is exported as the plain coroutine resume path.

## Status

Working:

- Boot into the game and the Lua main menu realm; gamemode discovery, script
  enumeration and loading through the engine file system (mounted archives
  included) with a local disk fallback.
- ConVar bridge sharing the engine's cvar registry; module allocations routed
  through the engine heap; a LuaFile cache with timestamp revalidation.

In development:

- The client and server Lua realms (scripts inside a running map).
- The GMod-standard C-side libraries (`file`, `sql`, `net`, `util`, ...).

## Build (Windows x64, MSVC)

Requirements: Visual Studio 2022 Build Tools, Source SDK public headers
(`public/`, `public/tier1/`, `public/tier0/`) for the convar bridge, and the
sibling `gmodcommon/include` headers.

```
cd src
call msvcbuild.bat gc64
```

The `gc64` flag is **required**: the platform layer is written for 64-bit
LuaJIT with the GC64 addressing mode, and the interface back pointer lives at
a different struct offset without it.

The build produces `src/lua51.dll`. Deploy it as
`GarrysMod/bin/win64/lua_shared.dll` (and `lua51.dll.pdb` as
`lua_shared.pdb` if you need symbols).

## Tree layout

```
src/
  lj_*.c, lib_*.c      LuaJIT 2.1 core and standard libraries
  lib_gmod.c           ILuaShared / CLuaInterface / CLuaShared, script loading,
                       mount registry, FindAndRunScript
  lib_luashared.cpp    LuaFile cache, engine file system bridge, search path
                       mounts, FindScripts
  lib_convar.cpp       ConVar / ConCommand bridge (engine-compatible layout)
  lib_mem.cpp          engine-heap allocation helpers
../gmodcommon/         shared headers used by the platform layer
```

## Tests

    cd src
    luajit.exe ..\tests\glua_syntax_test.lua
    luajit.exe ..\tests\glua_lib_test.lua
    luajit.exe ..\tests\glua_continue_edge_test.lua

Each script prints PASS/FAIL lines and exits non-zero on failure.

## License

Upstream LuaJIT code stays MIT (see the file headers and the upstream history
this branch is based on). The platform layer is provided for interoperability
as a clean-room implementation. Not affiliated with or endorsed by Facepunch
Studios. Garry's Mod is a trademark of Facepunch Studios.
