# opglua 工作须知（给后续会话/代理）

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
  `FileExists=10 / GetFileTime=13 / Open=2 / Read=0 / Close=3 / Size(handle)=6`；
  own 块 = 对象头，`AddSearchPath=11 / RemoveSearchPaths=14 / MarkPathIDByRequestOnly=15 /
  IsDirectory=22 / FindNext=32 / FindIsDirectory=33 / FindClose=34 / FindFirstEx=35`。
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
- **日志**：`OPGLUA_LOG` 环境变量非零开启；**禁止逐行 fflush**（1.4GB 日志 + 游戏无响应）。
  已知缺陷：经 Steam `-applaunch` 拉起会丢环境变量，文件旗标开关待实现。

## 当前未解决（下一步从这里开始）

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
3. **日志量**：`glua_log` 已分两级——lifecycle（旗标文件/`OPGLUA_LOG` 任意值）只记
   生命周期事件与内存心跳，verbose（`OPGLUA_LOG=verbose` 或旗标文件写 verbose）才记
   每次槽调用。**不要把 verbose 留在游戏运行中**：一次地图加载 120 万行，游戏会卡死。

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
