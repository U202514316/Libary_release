# Libary_release

## C++ → 蓝图调用示例（UE 5.1.1）

`Source/Libary_Release/CppBridgeLibrary.h/.cpp` 提供 `UCppBridgeLibrary::SayHello`。
它是一个静态蓝图函数，无需创建 C++ 对象，也无需修改现有蓝图的父类。

### 编译

关闭 UE 编辑器后，在 PowerShell 中执行：

```powershell
& 'D:\epic\UE_5.1\Engine\Build\BatchFiles\Build.bat' Libary_ReleaseEditor Win64 Development '-Project=D:\unreal_project\Libary_release\Libary_Release.uproject' -WaitMutex
```

首次添加这个类后，应先完成上述编译，再打开项目，以便编辑器加载新的类和函数。
若需要让 Visual Studio 的解决方案显示新增文件，可右键 `.uproject` → Generate Visual Studio project files。

本仓库已设置 `git config --local core.quotepath false`，避免 UE 5.1 构建工具解析 Git 输出中的中文路径时出错。
重新克隆仓库后，如遇到 `Path fragment ... contains invalid directory separators`，在项目目录执行该命令后重新编译。

### 在 UE 中调用

1. 用 UE 5.1 打开 `Libary_Release.uproject`，打开要运行的关卡。
2. 在顶部蓝图菜单选择 **Open Level Blueprint（打开关卡蓝图）**。
3. 在事件图表中找到或添加 `Event BeginPlay`，右键搜索 **Cpp Say Hello** 并添加该节点。
4. 将 `Player Name` 填为 `Xingcheng`，再添加 `Print String` 节点。
5. 将白色执行线接成 `Event BeginPlay → Cpp Say Hello → Print String`；
   将 `Cpp Say Hello` 的 `Return Value` 接到 `Print String` 的 `In String`。
   若 BeginPlay 已有逻辑，可通过 `Sequence` 增加一条执行分支，保留原有连线。
6. 把 `Print String` 的 `Duration` 设为 `10`，确认 `Print to Screen` 已勾选，编译蓝图并点击 **Play**。

画面应显示：

```text
Hello, Xingcheng! C++ is working.
```

在 **Window → Developer Tools → Output Log（输出日志）** 中搜索 `CppBridge`，应看到：

```text
LogTemp: [CppBridge] Hello, Xingcheng! C++ is working.
```

`Player Name` 留空时会使用 `Unreal`。此示例验证蓝图输入参数、C++ 执行、返回值以及蓝图显示结果的链路。

若找不到节点，确认 C++ 编译成功后已重新打开 UE 5.1 项目，并在关卡蓝图的事件图表中搜索 `Cpp Say Hello`。
也可以搜索 `Say Hello` 或分类 `Cpp Bridge`；必要时暂时取消右键菜单中的 `Context Sensitive（情境关联）`。

实现方式参考 Epic 的 [UE 5.1 Blueprint Function Libraries 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-function-libraries-in-unreal-engine?application_version=5.1)。

## 程序 A：经营逻辑接入

经营 C++、逐册秘密书、七条律令、表驱动结局及 Release 八张数据表的说明见 [程序 A C++ 接入说明](Docs/程序A_C++接入说明.md)，也可下载 [程序 A 逻辑完成情况与接口使用指南（Word）](Docs/程序A逻辑完成情况与接口使用指南.docx)。文档分别列出代码实现、UE 自动生成内容和人工 UI 接线，并提供 UE 5.1 中通过 `ShopPlayerController` / `ShopGameMode` 接入现有根 Widget 的点击步骤，以及返回值、回调与按钮前置阶段的注意事项。

2026-10-06：Editor 和 Game 目标完整构建均退出码 0；Release 八表已在 `/Game/ProgramA/Release/Data` 生成并通过独立进程校验，项目的八个引用和 Cook 配置已切换。最终 29 项自动化测试全部成功，报告中警告、失败、未运行均为 0，生成时间为 `2026.10.06-10.31.34`。证据见 [最终测试报告](Docs/程序A测试报告_20261006.json) 和接入说明末尾记录。

备用 Prototype 和以上 Hello 示例保留。程序 B 现有 UI 接线、关卡配置、PIE 人工试玩和 Windows 打包仍待验收；历史按本轮决定暂缓，集市、夜枭正式内容及 P2 存档不在本轮完成声明中。
