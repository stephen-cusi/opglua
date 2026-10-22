# opglua

[English](README.md) | [简体中文](README.zh-CN.md)

opglua is a source-buildable replacement for Garry's Mod's `lua_shared.dll`.
It combines LuaJIT 2.1 in GC64 mode with a GLua language layer and the native
interfaces used by the game and binary Lua modules.

The goal is to restore the reference runtime's behavior while keeping the
implementation available as source. The current Windows x64 build runs inside
Garry's Mod without modifying its engine or Lua content. Development and release
work use a single branch: `main`.

## Highlights

- **GLua syntax on LuaJIT:** `continue`, C-style logical operators and comments,
  plus compatible standard-library behavior.
- **Native host interfaces:** `ILuaShared`, `ILuaInterface`, `ILuaConVars`, and
  the C exports consumed by the game and binary modules.
- **Workshop Lua loading:** mounted archive discovery, script byte reads,
  autorun loading, and the file cache used by includes and `AddCSLuaFile`.
- **Native userdata and coroutines:** host metatables and callbacks remain
  usable across coroutine yields and protected Lua errors.
- **Self-contained build inputs:** required console support sources and headers
  live in `vendor/`; building does not switch or modify an engine checkout.
- **Quiet by default:** optional launch-argument diagnostics, with ordinary Lua
  errors and `print` output preserved in the game console.

## GLua language support

The language and library layer extends the LuaJIT 2.1.0-beta3 base:

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

## Current validation

| Area | Verified behavior |
| --- | --- |
| Game startup | Menu, client and server realms initialize; map entry and sky rendering have been confirmed in game. |
| Workshop addons | Enabled Workshop Lua addons load in the tested local game session; confirmed by the tester after the archive enumeration and read fixes. |
| Undo and console commands | Z undo has been confirmed in game; realm flags, callbacks and console object layout have native contract coverage. |
| Native objects | Player methods, coroutine calls and recovery after protected errors have been confirmed with in-game probes. |
| Binary modules | A purpose-built module passes loading, binary-string, reference, multiple-return, error and coroutine tests; in-game probe results were also confirmed. |
| File loading | Both the reference DLL and replacement pass mounted-script enumeration, byte-read and actual `FindAndRunScript` execution contracts. |
| Diagnostics | Default-off, explicit-off, invalid-option, lifecycle and verbose modes pass separate process tests. |

### Scope and remaining work

- The delivered and tested target is **Windows x64**. Linux and Android are not
  currently validated replacement builds.
- This is not yet a claim of complete compatibility with every addon, binary
  module or multiplayer scenario. Third-party modules must be installed
  separately; the test module is not MySQLOO or another third-party dependency.
- The current Windows build disables LuaJIT FFI.
- Garry's Mod still supplies the game libraries, rendering, entities and Steam
  integration. opglua replaces the Lua runtime; it is not a standalone game.

`docs/lua_shared_structure.md` maps the reference object model, all five
virtual tables, the interface memory layout, the boot sequence and the script
resolution chain, and tracks how far this implementation follows them.

## Build (Windows x64, MSVC)

Requirements: Visual Studio 2022 Build Tools, Python with `pefile`, and the
host's Windows x64 `tier0.dll` and `vstdlib.dll`. Console support sources and
GarrysMod headers are included in `vendor/`; no external engine checkout is
read, linked, or switched by the build.

```
set OPGLUA_HOST_BIN=D:\Program Files (x86)\Steam\steamapps\common\GarrysMod\bin\win64
build_windows.bat
```

The entry point prepares local import libraries from the host DLL exports,
then builds the runtime and its console support from source. To run the
actual-DLL contracts against both the replacement and reference, use
`tools\test_windows.bat`.

The `gc64` flag is **required**: the platform layer is written for 64-bit
LuaJIT with the GC64 addressing mode, and the interface back pointer lives at
a different struct offset without it.

The build produces `src/lua51.dll`. Deploy it as
`GarrysMod/bin/win64/lua_shared.dll` (and `lua51.dll.pdb` as
`lua_shared.pdb` if you need symbols).

## Diagnostic logging

Logging is off by default, including startup markers. Normal game launches
do not create diagnostic files. Lua errors and script `print` output still
reach the game's normal console.

Use these Steam launch options or executable arguments when diagnosing a
problem:

- `-opglua-log 1`: lifecycle messages, errors, and periodic memory summaries.
- `-opglua-log 2`: detailed interface and script-loading traces.
- `-opglua-log 0`: explicitly disable logging.
- `-opglua-logfile "D:\logs\opglua.log"`: optionally select a different file;
  the parent directory must already exist.

The default file is `opglua.log` beside the host executable. Detailed traces
can be large; remove the logging option after diagnosis and restart the game.
The old `OPGLUA_LOG` environment variable and `opglua_enable` flag file no
longer enable logging.

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
vendor/gmodcommon/    shared headers used by the platform layer
vendor/source-sdk/    local console support sources and headers
```

## Tests

Run the native interface, packed-script, binary-module and logging contracts:

```bat
tools\test_windows.bat
```

The reference DLL tests use a copy named `lua_shared,.dll` in the configured
host directory. This is a fixture name, not the filename loaded by the game.
Native contracts validate interface behavior; they do not replace in-game
testing of complete addons and multiplayer scenarios.

Language and standard-library tests:

    cd src
    luajit.exe ..\tests\glua_syntax_test.lua
    luajit.exe ..\tests\glua_lib_test.lua
    luajit.exe ..\tests\glua_continue_edge_test.lua

Each script prints PASS/FAIL lines and exits non-zero on failure.

## License

Upstream LuaJIT code stays MIT (see the file headers and upstream history).
Dependencies in `vendor/` retain their own copyright and license notices.
The platform layer targets interoperability with the host. This project is not
affiliated with or endorsed by Facepunch Studios. Garry's Mod is a trademark
of Facepunch Studios.
