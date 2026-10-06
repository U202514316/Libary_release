# Libary_release

## 当前玩法：秘密书上架到表书店（2026-10-06 修订）

界面由用户在控件蓝图中制作。C++ 已移除原生文字演示界面及其专用测试，只提供业务接口及可选的根 Widget 创建/注册。最新目标流程是“菜单 → 开场引导 → 白天 3 位顾客（可含秘密顾客）→ 夜间进货/里店上架二选一 → 次日营业”；白天秘密顾客、固定人数、教程衔接及律令资源来源的适配尚未完成，下文阶段表描述的是当前底层能力。

所有顾客和交易都发生在表书店。里书店不经营，已支持秘密书上架/撤回；阅读、收取、律令等既有 C++ 能力保留，是否接入由控件蓝图流程决定。此前 Word 计划书中“里书店营业 / InsideSell / 里店顾客”的描述已过时。

当前底层尚保留上一版规则：秘密顾客只在夜晚出现。流程为：白天营业 → 日结继续 → 选择里店管理 → 上架秘密书 → 返回夜间表书店 → 接待秘密顾客并售卖 → 夜结。选择进货的夜晚也可返回表书店，出售此前已上架的秘密书；同一夜的活动选择仍互斥。夜间往返管理页不会重新抽顾客，在管理页不会扣顾客耐心。

### 使用自己的控件蓝图

在项目实际使用的 `BP_ShopPlayerController → Class Defaults` 中，将 **Root Widget Class** 设置为用户制作的 `WBP_UIRoot`。根控件需要实现原生 **ShopView** 接口，PlayerController 会创建、显示并注册它；根控件不要重复注册。主菜单、书架、商人和律令页由根控件中的子 Widget 实现。

如果希望连根控件的创建也由蓝图负责，将 **Root Widget Class** 留空，C++ 不会自动创建界面；创建自己的根控件后调用 `AttachShopView(View)` 可统一显示并注册。两种创建方式选一种。旧版 **Use Secret Trade Demo UI** 开关已从代码移除，旧设置不会再覆盖根控件。当前仍打开的旧编辑器必须完整编译并重新打开才会加载此变更；在旧版本中可先取消勾选该开关。

需要先显示主菜单时，在实际使用的 `BP_ShopGameMode` 中关闭 **Start New Run On First Entry**；开场引导结束后由按钮逻辑调用 `RequestNewRun`。以下新增接口可从控件蓝图调用。

| 按钮/查询 | 原生调用 | 条件与效果 |
| --- | --- | --- |
| 进入里店管理 | `RequestOpenInside()` | 黄昏选择里店；或当晚已选择里店时，从 `NightShop` 返回。里店没有交易和顾客计时。 |
| 上架一册 | `RequestListSecretBook(BookId)` | 仅 `Inside`；从未上架副本中移动一册到表店在架状态。 |
| 撤回一册 | `RequestUnlistSecretBook(BookId)` | 仅 `Inside`；撤回一册已上架书，总库存不变。 |
| 返回表书店 | `RequestOpenTableShop()` | 从 `Inside/Restock` 进入 `NightShop`；同夜只生成一次顾客。 |
| 接待/售卖/取消 | `RequestBeginSell` / `RequestSell` / `RequestCancelSell` | `Day→Sell` 或 `NightShop→NightSell`；只操作当前队首。秘密顾客只买已上架秘密书。 |
| 已拥有/上架/存放数量 | `GetBookRuntime(BookId)` | 秘密书的 `Stock` 为总拥有数，`ListedCopies + StoredCopies = Stock`。普通书仍读 `Stock`。 |

三个新增命令均返回 `FShopCommandResult`。上架不收费、不增污，不修改静态 `FBookData.Layer`；秘密书成交才扣一册、按 `Price` 加钱并按 `PollutionOnSell` 增污，整笔失败不部分扣款。初始 7 种秘密书各 1 册，全部未上架；重开恢复这一状态。旧 `InsideCustomers` 字段为兼容既有表保留，编辑器显示 **Night Table Customers**，现在表示夜间表店人数。`InsideSell` 枚举值保留以避免旧资产值错位，但不再进入。

污染阈值与结局继续生效。根控件通过 `OpenCalmPanel` 显示用户制作的律令页面，按钮调用 `RequestEnactDecree`；若流程允许跳过，则调用 `RequestSkipDecree`。C++ 通知负责传递状态和结果，不负责生成律令页面。

本次取消原生界面验证（2026-10-06）：Editor 和 Game 的 Win64 Development 构建均成功（退出码 0），日志分别为 `Saved/Logs/BlueprintUIOnly_EditorBuild.log` 和 `Saved/Logs/BlueprintUIOnly_GameBuild.log`。演示界面及其专用测试删除后，36 项经营自动化测试全部成功，警告/失败/未运行均为 0；报告见 [自动化结果](Saved/Automation/BlueprintUIOnly_20261006_Final/index.json)。这些结果验证现有底层规则，不代表新版白天秘密顾客流程或用户控件蓝图已完成，也不代替 PIE 和打包验收。

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

以下为本次秘密书上架修改之前的历史验证：2026-10-06，Editor 和 Game 目标完整构建均退出码 0；Release 八表已在 `/Game/ProgramA/Release/Data` 生成并通过独立进程校验，项目的八个引用和 Cook 配置已切换。当时 29 项自动化测试全部成功，报告中警告、失败、未运行均为 0，生成时间为 `2026.10.06-10.31.34`。证据见 [历史测试报告](Docs/程序A测试报告_20261006.json) 和接入说明末尾记录；本次变更的验证状态见页首。

备用 Prototype 和以上 Hello 示例保留。程序 B 现有 UI 接线、关卡配置、PIE 人工试玩和 Windows 打包仍待验收；历史按本轮决定暂缓，集市、夜枭正式内容及 P2 存档不在本轮完成声明中。
