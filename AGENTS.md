# opglua 工作须知（给后续会话/代理）

## 2026-10-22 当前构建与修复

- 用户已实测确认 Workshop 插件加载正常，授权清理临时文件并提交推送。诊断现在默认关闭；启动参数 -opglua-log 1 开生命周期与错误日志，-opglua-log 2 开详细跟踪，-opglua-log 0 关闭，-opglua-logfile 指定路径。默认 opglua.log 在宿主可执行文件旁。旧环境变量 OPGLUA_LOG 和 opglua_enable 不再生效，关闭时不写启动标记。Lua 错误与 print 仍正常送游戏控制台；不要为了静默而吞掉脚本错误。
- 新日志确认 Workshop Lua 已被枚举（lsv autorun 文件系统 12 条、合并 19 条），但 minecraft.lua 等随后全部 MISS；不要继续把此状态归因于启用配置或挂载失败。第二层缺陷为 IBaseFileSystem::Size 重载顺序：实际 x64 主机 base slot 6=Size(name,pathID)，slot 7=Size(handle)，旧桥把句柄传给 slot 6，打包脚本读取长度失败，loose 文件被本地 fopen 回退掩盖。本次修正到 slot 7，新增读取及真实 FindAndRunScript 执行契约；修复前原版通过、替换版读取断言失败，修复后双方读取与执行均通过。证据 D:/tmp/glua/undo_20261022/packed_read_before.log、packed_execution_contracts.log。游戏内插件仍待用户实测，不宣称全部加载完成。
- Workshop 文件枚举的查找句柄必须使用 intptr_t。目标文件系统返回对象指针，FindFirstEx 写出指针宽度，FindNext/FindIsDirectory/FindClose 原样接收；旧 int 声明导致栈覆盖和指针截断，SEH 随后禁用文件系统桥，使 GMA Lua 不可见。本次已用同一指针高位非零的夹具复现旧替换版失败、原版通过，修正后两份 DLL 均通过。客户端查找 hook 的第 4 个参数明确传 false，无 hook 时客户端不回退文件系统；排序及允许/拒绝回退均有原版对照。记录见 D:/tmp/glua/undo_20261022/workshop_pointer_hook_contracts.log；尚待用户实测 Workshop 插件。
- 菜单 common.js 的 DigestUpdate 属于 Angular scope 刷新调度，不能直接视为 Steam API 回调或加密接口缺失。订阅面板等待、网络请求失败和 GMA Lua 枚举须分别取证；不要通过隐藏原版面板或修改订阅配置掩盖问题。
- 现在从仓库根执行 `build_windows.bat`，SDK 支持代码与 GMod 公共头已包含在 `vendor/`。构建不再引用 D:/project/source-engine、不切换共享引擎分支、不链接其静态库。`OPGLUA_HOST_BIN` 指向目标游戏的 win64 目录，仅用于生成 tier0/vstdlib 导入库。下文旧外部依赖说明仅为历史记录。
- 本地 ConVar 主表和 IConVar 次表已对齐，sizeof/实际方法调用由 `tests/gmod_abi/console_contract.cpp` 验证；不要将这些本地 ABI 改动写回共享引擎头。
- CallInternal 的返回对象按弹栈顺序保存，GetReturn(0) 对应最后一个 Lua 返回值。此契约已用同一 host 对照原版确认；不要按 Lua 源码返回顺序填入对象池。
- 宿主延迟调用队列已改为动态链表，保留尚未就绪项。RunStringEx 的 dontPushErrors 编译错误路径已修正；PushMetaTable 只按 type id 查表。
- `tools/test_windows.bat` 验证实际 DLL、宿主对象、元表方法、材质代理式双返回、四返回值、257 个延迟调用和报错后栈状态。两份 runtime 都需 failures=0；mock 服务不等于真实游戏通过。
- 用户已确认上一轮天空正常，并在命令标志修复部署后确认 Z 撤销恢复正常。CreateConCommand/CreateConVar 已补 realm 标志，服务端命令补齐 GAMEDLL、Lua 服务端与命令执行权限标志，客户端和菜单端按各自规则处理。命令标志、回调和参数对象身份也已对照原版实际 DLL 验证，未用修改 undo.lua 隐藏错误。
- 未自行启动游戏；上述天空与 Z 撤销结论来自用户实测。多人、换图、外部二进制模块与更广泛功能尚未全部验证。证据与部署记录在 D:/tmp/glua/undo_20261022。
- 控制台测试代码禁止直接粘贴含未引用分号的长 lua_run；Source 会把它拆成多条控制台命令。现提供 tests/gmod_abi/live_probe.lua，部署为游戏 lua/opglua_probe.lua，手动 lua_openscript / lua_openscript_cl 运行，不自动启动游戏。
- 语法报错回调必须传 ILuaGameCallback::CLuaError，不能将 const char* 当作对象。现由 C++ 构造 message/side/stack，再调用宿主 LuaError；RunString 必须转发 dontPushErrors=true/noReturns=true，RunStringEx 运行失败时 noReturns 也会清栈。原版与替换 DLL 已通过全部报错开关组合及保留栈值测试，不能把离线测试当成真实闪退已复测。细节见 D:/tmp/glua/undo_20261022/CRASH_FIX.md。
- Console 桥必须持有名称、帮助、默认值字符串，ConCommand/ConVar 构造函数不会复制全部输入。现用稳定字符串池持有，生命周期与注册对象一致。可变调用方缓冲区复用测试已覆盖。测试批处理须判断非零退出码，不能只用 if errorlevel 1，原生异常退出码可能是负数。
- print 已改为送到当前宿主 Msg 回调，原来写 stdout 导致探针执行成功却没有控制台输出。Workshop 根挂载使用文件系统 RelativePathToFullPath 解析结果，禁止将已能查询的相对虚拟路径直接当作完整路径注册。两项均有原版 DLL 对照证据。
- GMOD_LoadBinaryModule 已移除多余的临时 holder 栈值；Require 二进制路径复用同一加载器，Lua 模块经挂载路径查找，找不到模块会抛 Lua 错误供 pcall 捕获。自编二进制夹具覆盖入口、二进制字符串、引用、多返回值、原生错误恢复和协程；它不代表 MySQLOO 等第三方模块已实测。
- 用户新一轮明确报告 server/client 探针的 timer 回调均未执行。GMod 的 LuaClientDatatableHook 是 FindScripts 数据源；ILuaShared::FindScripts 必须对 lcl 调其 hook 的 FindScripts(path,out)，只有结果为空且 ShouldFallback=true 才继续文件系统。opglua 存了 SetLuaFindHook 却从未调用，这是 workshop client Lua autorun 缺失的根因。现已接通 hook；Filesystem contract 只验证 mounted search path，不仿造 hook 内部调用，避免假 hook ABI/CRT 污染。timer.Simple 排队而无回调目前是已知待定位现象，不把缺少的延迟输出误判为 player/协程用例失败。
- 本轮构建部署后，内置自编 opglua_contract 模块至 lua/bin 的 server/client 命名，测试脚本为 lua/opglua_binary_probe.lua，验证 module load、binary strings、Reference、multiple returns、native error、coroutine。不要把该测试模块当作 MySQLOO，也不要纳入仓库或提交；部署清单 D:/tmp/glua/undo_20261022/workshop_and_binary_deployment.json。
- 用户确认 Workshop 恢复后移除游戏目录中的临时 Lua 探针、自编测试模块及旧日志旗标；保留 tests/gmod_abi 的永久契约源代码。只推送 origin，不改共享引擎仓或插件启用配置，禁止自行启动游戏。


重写 GMod 的 `lua_shared.dll`：LuaJIT 2.1（GC64）+ 平台层，实现 ILuaShared / ILuaInterface /
ILuaConVars 与游戏、二进制模块消费的 C 导出。构建产物 `src\lua51.dll` 部署为
`GarrysMod\bin\win64\lua_shared.dll`（`lua51.dll.pdb` 改名 `lua_shared.pdb` 同拷）。
部署 = 直接拷贝覆盖，不留备份。

## 构建

- 入口：仓库外层 `D:\tmp\glua\_build_all.bat`（vcvars64 + `msvcbuild.bat gc64`）。
- **必须带 `gc64`**：非 GC64 构建里接口回指指针落在错误偏移，引擎模块一调就崩。
- `msvcbuild.bat` 会把 convar 桥（`lib_convar.cpp` + `%SRCENG%\tier1\convar.cpp` →
  `convar_impl.obj`）、`lib_luashared.cpp`、`lib_mem.cpp` 一起编进 DLL，并链
  tier1.lib + tier0.lib。`%SRCENG%` 指 Source SDK 公共头（public/、public/tier1/、public/tier0/），
  另需兄弟目录 `..\..\gmodcommon\include`。

## 参考二进制

- 真·原版 = `GarrysMod\bin\win64\lua_shared,.dll`（文件名含逗号，约 0.8MB）。
- `D:\games\garrysmod\bin\win64\lua_shared.dll` 是**另一分支**的版本，ILuaShared 虚表
  布局与 Steam 版引擎不配套（它无析构槽），**接口布局证据一律不要引用它**；
  它的文件系统槽位结论可引用（与真原版一致）。

## 接口契约（已对齐引擎，改动前先读）

- **ILuaShared 虚表 19 槽，槽 0 = 析构**：0 dtor / 1 Init / 2 Shutdown / 3 DumpStats /
  4 CreateLuaInterface / 5 CloseLuaInterface / 6 GetLuaInterface / 7 LoadFile / 8 GetCache /
  9 MountLua / 10 MountLuaAdd / 11 UnMountLua / 12 SetFileContents / 13 SetLuaFindHook /
  14 FindScripts / 15 GetStackTraces / 16 InvalidateCache / 17 EmptyCache / 18 ScriptExists。
  引擎行为验证过：Init/MountLua/CreateLuaInterface 按此命中（实现见 `lib_gmod.c`
  `glua_shared_vtable`）。
- **IFileSystem（VFileSystem022）**：双子对象，base 块 = 对象指针 + 1 个槽位处，
  `FileExists=10 / GetFileTime=13 / Open=2 / Read=0 / Close=3 / Size(name,pathID)=6 / Size(handle)=7`；
  own 块 = 对象头，`AddSearchPath=11 / RemoveSearchPaths=14 / MarkPathIDByRequestOnly=15 /
  IsDirectory=22 / FindNext=32 / FindIsDirectory=33 / FindClose=34 / FindFirstEx=35`。
  查找句柄是 intptr_t，不是 Source SDK 旧头中的 int；不得按查找对象首字段的 32 位属性读取推断句柄宽度。
  AddSearchPath 的 addType：普通 5、workshop 根 0x105（head 模式）。
  所有调用包 SEH 守卫，任一异常即置不可用、回退本地磁盘遍历。
  **`__try` 不能与需要展开的 C++ 对象同帧（C2712）**——守卫调用隔离进只收指针的帧。
- **MountLua 语义**：恒挂 `lua`；pathID 精确等于 `LuaMenu` 时不再挂 `gamemodes`。
  **挂载前先把 root 解析成文件系统打得开的目录**（绝对路径优先），从不把裸相对路径
  交给引擎；解析失败退 `<mod 目录>/<root>`（mod 目录从宿主可执行文件位置推：
  bin/win64 上退两级 + garrysmod）。
- **LoadFile / LuaFile 缓存**：命中且时间戳一致时返回**同一对象**；计数器只在创建时清零、
  归引擎所有；`source` 文件读为空；缓存键与存储名 = 小写化 + 去 `workshop/` 前缀，
  但**文件系统查询用原始路径**（剥前缀只影响缓存身份与存储名）；空文件/找不到 → NULL。
  `GetCache` 是纯查表（未命中 NULL，不触发读盘）。
- **FindScripts**：输出 basename + isFolder（`LuaFindResult`，0x28 stride），跳过点项，
  跨 root 去重，按文件名排序。
- **实际加载路径的真相**：引擎几乎不调 ILuaShared 的文件函数，脚本全走
  `CLuaInterface::FindAndRunScript`。我们的解析顺序 = 引擎文件系统优先
  （`opglua_luashared_readfile`：带 pathID 再不带），本地磁盘遍历兜底
  （`glua_open_script_id` / `opglua_read_script_id`）。

## 布局与运行契约

- **lua_State（GC64）**：接口回指 `luabase` 在 +0x78（120），`sizeof = 0x80`。
  公共 `Interface.h` 里 `92 + 22` 的占位注释是**过时的**（应为 +28 → 120）。
  新建协程状态必须：拷贝父状态的 `luabase` + 清 `gm_reserved[0..2]`
  （`lj_state.c`），否则协程里调引擎回调即崩。
- **ConVar 必须按引擎布局 0x90**：回调向量（CUtlVector）在 +0x70，浮动值域在其前；
  布局短了引擎回写回调时踩值域 = OOM / DEP-EXECUTE。ConVar/ConCommandBase 的
  Create/CreateBase 均为虚函数，竞争性函数桩 false。
- **ConVar 的次虚表（IConVar 子对象）在原版是 8 槽**：`SetValue(Color) / SetValue(int) /
  SetValue(float) / SetValue(const char*) / GetName / GetName / IsFlagSet(int) / 恒 false 桩`，
  顺序就是这四参类型的反常识排序（**不是** str/float/int/Color）。槽数不足时，
  引擎按 `IsFlagSet` 的签名（一个 int 参数、bool 返回）调进来的那次会落在空槽上硬崩。
  主表（ConCommandBase 部分）原版是 12 槽——`Init` 在那边**不是虚函数**，
  多声明一个会让整张主表错位一格。
- **IMemAlloc 释放虚表**：`[0]=Alloc / [2]=Realloc / [4]=Free`（调试/发布成对）。
  模块侧跨引擎分配一律走 `opglua_mem_alloc/free`（lib_mem.cpp），不做虚表挂钩。
- **池化字符串表**按原版槽位数量建（323），不要缩容，越界 NULL 会崩引擎。
- **元表栈协议**：`CreateMetaTable / CreateMetaTableType / RegisterMetaTable` 契约是
  把表留在栈上（-1）；RegisterMetaTable 恒注册传入的表（保留既有 MetaID）。
- **日志**：默认关闭，以启动参数 -opglua-log 1/2 开启；旧环境变量和旗标不再控制日志。禁止逐行 fflush，详细跟踪只用于短时诊断。

## 历史问题快照（状态以上方记录为准）

以下为早期接手记录。ConVar ABI、延迟队列、报错桥与 Workshop 脚本读取已经修正，不能据此重复回退已验证代码。

0. **ConVar 的 IConVar 子对象虚表槽数不足（最高优先级，先修这个）**。该子对象在原版
   有 8 个槽位，当前头文件只声明了 5 个，多出来的调用落在空槽上。引擎在脚本跑完后
   会做一次全量 cvar 遍历，正好命中，表现为进图后期硬崩。
   取证细节见本地 `D:\tmp\glua\HANDOFF.md` 第 3 节（含逐槽语义与修改步骤）。
   连带要核对：`ConCommandBase` 主表在原版是 12 槽（`Init` 不是虚函数），
   当前多一格，导致主表整体错位。**改完这两个表之后所有 ConVar 相关报错
   （`GetConVar` 返回 nil、spawnmenu 的 `self.ToolToggle` 为 nil）也可能一并消失。**
1. **进图黑屏 / Q 菜单不出**。三个 realm 现在都创建成功（菜单、客户端、
   服务器），脚本经引擎文件系统正常加载，日志停在 `sandbox/gamemode/persistence.lua`。
   先修第 0 条；之后再考虑：① `Cycle`（slot 57）必须真的泵送 `AddThreadedCall`
   队列——现在实现是清空丢弃，客户端每张图调用一次；② 逐槽核对 `CLuaInterface`
   132 个槽的行为（不只是签名），优先 `GetPooledString`/`PushPooledString`、
   `CallInternal*` 族、`GetReturn`、`NewTemporaryObject`；③ realm=1 加载了 0 个
   `cl_init.lua` 而 realm=0 加载了 12 个，这个不对称要查清楚。
2. **workshop 地图启动卡死**：`Couldn't write cache/map_pack.bsp` 后无响应；普通地图可
   完整跑完退出。排查引擎文件系统优先读路径在 BSP 挂载期的重入。
3. **日志量**：现为默认关闭，-opglua-log 1 开诊断，-opglua-log 2 开详细跟踪。历史环境变量及旗标机制已经移除，详细跟踪不应常驻开启。

## 交接

`D:\tmp\glua\HANDOFF.md` 是取证级交接文档（允许写地址与偏移）：现状、已修提交、
未修根因的完整证据链、工具坑、未完成清单。接手前先读它。

## 结构档案

`docs/lua_shared_structure.md` 是原版对象模型、两个虚表、Init 启动序列的整理版
（白盒措辞、无地址）。逐槽名字表与函数正文在本地取证目录
`D:\tmp\dec\lua_shared.dll\`（`CLuaInterface.vtable_slots.txt`、
`ILuaBase_slot_map.txt`、`lua_State_layout.txt`、`ILuaShared_contract.txt`）。
定位方法：**用 RTTI 求 vtable 才是权威**——`CLuaInterface` = 132 槽、
`CLuaShared` = 19 槽、`CLuaConVars` = 8 槽；.rdata 里还有一张同尺寸的
`CLuaInterface_Debug` 表，别把两张搞混（否则摸到的是另一套实现）。


## 工具与既有脚本

- 反汇编 `D:\tmp\glua\_dis2.py <dll绝对路径> <RVA十六进制> <字节数> [ret数]`；
  读字符串 `_str.py <dll> <VA>...`；一次性扫描用 pefile + capstone。
- 坑：pefile 查字符串得到的是 RVA（加 ImageBase 才是 VA），比对目标时统一量纲，
  混用会得到"零命中"的假结论。

## 铁律

1. 代码、注释、提交信息、任何入库文本：**禁止 emoji**；**禁止出现 VA/RVA 地址、
   FUN_ 名、"decompile/disassembly/反编译" 措辞**——行为一律白盒描述
   （"原版/the reference does X"）；取证细节只写会话报告或本地文档，不进仓。
2. 禁止分析 crash dump / minidump。
3. 推送只推 `origin`（opglua 仓库）；`upstream` 是 LuaJIT 官方，**绝不推**。
4. 启动游戏用 python `subprocess.Popen` 非阻塞拉起，**禁止长 sleep 等待**；
   验证以日志为准（`console.log` 经 `-condebug`，opglua 日志见上）。
