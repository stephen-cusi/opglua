# OPGLua

GLua-compatible Lua runtime, built on LuaJIT 2.1.

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

## Build (Windows x64, MSVC)

    opglua_build.bat

Produces `src\luajit.exe` and `src\lua51.dll`.

## Tests

    cd src
    luajit.exe ..\tests\glua_syntax_test.lua
    luajit.exe ..\tests\glua_lib_test.lua
    luajit.exe ..\tests\glua_continue_edge_test.lua

Each script prints PASS/FAIL lines and exits non-zero on failure.

## License

Upstream LuaJIT code stays MIT (see the file headers and the upstream
history this branch is based on).
