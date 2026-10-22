# opglua

[English](README.md) | [简体中文](README.zh-CN.md)

opglua 是 Garry's Mod `lua_shared.dll` 的可从源码构建的替代实现，基于
LuaJIT 2.1 GC64，提供 GLua 语言扩展，以及游戏和二进制 Lua 模块使用的原生接口。

项目目标是还原原版运行时行为，并保留可维护的源码实现。当前 Windows x64
版本运行于 Garry's Mod 内，不需要修改其引擎或 Lua 内容。开发与发布统一使用 `main` 分支。

## 项目特色

- **LuaJIT 上的 GLua 语法**：支持 `continue`、C 风格逻辑运算符与注释，并补齐相关标准库行为。
- **原生宿主接口**：实现 `ILuaShared`、`ILuaInterface`、`ILuaConVars`，以及游戏和二进制模块使用的 C 导出。
- **创意工坊 Lua 加载**：支持挂载归档的文件发现、脚本读取、autorun，以及 include 和 `AddCSLuaFile` 使用的缓存。
- **原生对象与协程**：宿主元表和方法可以跨协程挂起、恢复及受保护的 Lua 错误继续使用。
- **独立构建输入**：必要的控制台支持源码与头文件包含在 `vendor/`，构建不切换或修改外部引擎仓库。
- **默认静默**：通过启动参数按需打开日志；正常 Lua 报错和 `print` 仍输出到游戏控制台。

## GLua 语言支持

语言与标准库扩展基于 LuaJIT 2.1.0-beta3：

- `continue` 支持数值 for、泛型 for、while、repeat 及嵌套循环，跳转到所属循环的继续位置。
- `&&`、`||`、`!`、`!=` 对应 Lua 的逻辑和比较运算。
- 支持 `//` 行注释和不嵌套的 `/* */` 块注释。
- `table.insert` 返回插入位置，解释执行与 JIT 路径保持一致。
- C API 创建的元表记录 `MetaName` 和 `MetaID`，支持按类型 ID 注册的 `luaL_newmetatable_type`。
- 配合 Lua 层的 `type()` 扩展，userdata 可以使用元表名称报告类型。
- 导出 `lua_resume_real`，提供普通协程恢复入口。

## 当前验证状态

| 范围 | 已验证内容 |
| --- | --- |
| 游戏启动 | 菜单、客户端和服务端 Lua 域初始化；用户已实测进入地图及天空渲染。 |
| 创意工坊插件 | 修正归档枚举和读取后，用户已确认测试的本地游戏会话中启用的 Workshop Lua 插件可以加载。 |
| 撤销与控制台 | 用户已确认 Z 撤销恢复；realm 标志、回调及控制台对象布局有原生契约测试覆盖。 |
| 原生对象 | 玩家方法、协程调用、受保护错误后的原生调用已通过游戏内探针验证。 |
| 二进制模块 | 自编测试模块覆盖加载、二进制字符串、引用、多返回值、错误及协程；游戏内探针结果也已确认。 |
| 文件加载 | 原版和替换 DLL 均通过挂载脚本枚举、字节读取及真实 `FindAndRunScript` 执行测试。 |
| 日志 | 默认关闭、显式关闭、无效参数、诊断模式、详细模式均有独立进程测试。 |

### 范围与限制

- 当前交付和验证的目标是 **Windows x64**，尚未验证 Linux 或 Android 替代版本。
- 尚不宣称兼容所有插件、二进制模块及多人场景。第三方模块需要单独安装；自编测试模块不是 MySQLOO 等第三方依赖。
- 当前 Windows 构建禁用 LuaJIT FFI。
- 游戏库、渲染、实体和 Steam 集成仍由 Garry's Mod 宿主提供；本项目是 Lua 运行时替代实现，不是独立游戏。

对象结构和接口资料见 [lua_shared 结构说明](docs/lua_shared_structure.md)。

## 构建

需要 Visual Studio 2022 Build Tools、安装了 `pefile` 的 Python，以及目标游戏的
Windows x64 `tier0.dll` 和 `vstdlib.dll`。控制台支持源码和 GarrysMod 头文件已包含在仓库中。

```bat
set "OPGLUA_HOST_BIN=D:\Program Files (x86)\Steam\steamapps\common\GarrysMod\bin\win64"
build_windows.bat
```

脚本从宿主 DLL 导出生成本地导入库，然后编译运行时与控制台支持代码，不依赖外部引擎构建产物。
构建入口已经启用必需的 GC64 模式。

输出为 `src/lua51.dll`，部署到游戏的 `bin/win64/lua_shared.dll`。
需要调试符号时，将 `src/lua51.dll.pdb` 同时部署为 `lua_shared.pdb`。

## 日志开关

默认不创建日志文件，也不写启动标记。需要诊断时，在 Steam 启动选项或程序参数中使用：

| 参数 | 行为 |
| --- | --- |
| `-opglua-log 1` | 生命周期、错误和定期内存摘要。 |
| `-opglua-log 2` | 详细接口调用与脚本加载跟踪。 |
| `-opglua-log 0` | 显式关闭日志。 |
| `-opglua-logfile "D:\logs\opglua.log"` | 指定日志路径，父目录需要预先存在。 |

默认日志为宿主可执行文件旁的 `opglua.log`。详细模式日志量较大，诊断结束后移除参数并重启游戏。
旧 `OPGLUA_LOG` 环境变量和 `opglua_enable` 旗标文件不再生效。

## 测试

原生接口、挂载文件和二进制模块契约测试：

```bat
tools\test_windows.bat
```

测试原版 DLL 时，脚本使用目标游戏目录中名为 `lua_shared,.dll` 的参考副本。
此名称用于测试夹具，不是游戏运行时的加载名称。测试日志开关的脚本也会自动执行。

语言与标准库测试：

```bat
cd src
luajit.exe ..\tests\glua_syntax_test.lua
luajit.exe ..\tests\glua_lib_test.lua
luajit.exe ..\tests\glua_continue_edge_test.lua
```

各脚本输出 PASS/FAIL，失败时以非零状态退出。离线契约测试验证接口和数据行为，不能替代完整游戏场景测试。

## 目录

| 目录或文件 | 用途 |
| --- | --- |
| `src/lj_*.c`、`src/lib_*.c` | LuaJIT 核心、标准库与 GLua 扩展。 |
| `src/lib_gmod.c` | 宿主接口、调用与模块加载。 |
| `src/lib_luashared.cpp` | 脚本缓存、文件系统桥、挂载和枚举。 |
| `src/lib_convar.cpp` | ConVar 与 ConCommand 桥接。 |
| `src/lib_mem.cpp` | 宿主内存分配器桥接。 |
| `vendor/` | 构建使用的本地支持源码和头文件。 |
| `tests/`、`tools/` | 回归测试与构建工具。 |

## 许可

LuaJIT 上游代码保留 MIT 许可，详见源码中的版权说明和上游历史。
`vendor/` 中的依赖保留各自版权及许可声明。本项目用于互操作，不隶属于 Facepunch Studios，
也不代表其背书；Garry's Mod 是 Facepunch Studios 的商标。
