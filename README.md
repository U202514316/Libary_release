# Libary_release

## 当前玩法：秘密书上架到表书店（2026-10-06 修订）

所有顾客和交易都发生在表书店。里书店在本次范围内只管理秘密书上架/撤回；阅读、收取、律令等旧能力保留在代码中，但不接入这次简易界面的操作流程。此前 Word 计划书中“里书店营业 / InsideSell / 里店顾客”的描述已被本节取代。

秘密顾客仍只在夜晚出现。流程为：白天营业 → 日结继续 → 选择里店管理 → 上架秘密书 → 返回夜间表书店 → 接待秘密顾客并售卖 → 夜结。选择进货的夜晚也可返回表书店，出售此前已上架的秘密书；同一夜的活动选择仍互斥。夜间往返管理页不会重新抽顾客，在管理页不会扣顾客耐心。

### 编译后直接操作

`ShopPlayerController.bUseSecretTradeDemoUI` 默认开启，运行当前 `/Game/Test` 时由 C++ 创建 `ShopSecretTradeWidget`，提供菜单、表店交易、里店上架、进货、结算、污染提示及结局重开按钮。它沿用项目当前的 35 天配置，没有把正式数据改为 3 天，也不保证每晚一定抽到秘密顾客。

这个简易界面不修改已有 `WBP_UIRoot` 等资产。准备使用自己的正式界面时，在 `BP_ShopPlayerController → Class Defaults` 关闭 **Use Secret Trade Demo UI**，即可继续使用原 `Root Widget Class`。原控件需要按以下新接口接线。

| 按钮/查询 | 原生调用 | 条件与效果 |
| --- | --- | --- |
| 进入里店管理 | `RequestOpenInside()` | 黄昏选择里店；或当晚已选择里店时，从 `NightShop` 返回。里店没有交易和顾客计时。 |
| 上架一册 | `RequestListSecretBook(BookId)` | 仅 `Inside`；从未上架副本中移动一册到表店在架状态。 |
| 撤回一册 | `RequestUnlistSecretBook(BookId)` | 仅 `Inside`；撤回一册已上架书，总库存不变。 |
| 返回表书店 | `RequestOpenTableShop()` | 从 `Inside/Restock` 进入 `NightShop`；同夜只生成一次顾客。 |
| 接待/售卖/取消 | `RequestBeginSell` / `RequestSell` / `RequestCancelSell` | `Day→Sell` 或 `NightShop→NightSell`；只操作当前队首。秘密顾客只买已上架秘密书。 |
| 已拥有/上架/存放数量 | `GetBookRuntime(BookId)` | 秘密书的 `Stock` 为总拥有数，`ListedCopies + StoredCopies = Stock`。普通书仍读 `Stock`。 |

三个新增命令均返回 `FShopCommandResult`。上架不收费、不增污，不修改静态 `FBookData.Layer`；秘密书成交才扣一册、按 `Price` 加钱并按 `PollutionOnSell` 增污，整笔失败不部分扣款。初始 7 种秘密书各 1 册，全部未上架；重开恢复这一状态。旧 `InsideCustomers` 字段为兼容既有表保留，编辑器显示 **Night Table Customers**，现在表示夜间表店人数。`InsideSell` 枚举值保留以避免旧资产值错位，但不再进入。

污染阈值与结局继续生效；本次简易界面在已有 `Calm` 时提供“暂不处理，继续”，不会要求先实现律令界面才能继续交易。UI 不自动颁布律令，也不自动清除污染。完整律令/阅读/收取界面属于后续范围。

当前验证（2026-10-06）：最新代码的 `Libary_Release Win64 Development` 构建成功（退出码 0），包括新增界面和全部测试代码；日志为 `Saved/Logs/SecretTrading_GameBuild.log`。当前共有 37 项 `Bookstore.ProgramA` 自动化测试，尚待关闭正在运行的 UE 编辑器后完整编译 Editor 并执行；编译通过不代表测试已通过，也不代表已完成 PIE 画面和打包验收。历史 29 项结果只对应旧版本。

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
