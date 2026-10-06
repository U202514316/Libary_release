# Libary_release

## 当前玩法与控件蓝图入口（2026-10-06 修订）

`/Game/ProgramA/UI`已创建并接通 **15 个可编辑控件蓝图**，包含真实 Designer 控件树与 EventGraph 按钮连线。当前闭环为“主菜单 → 开场引导 → 白天 3 个顾客槽位 → 白天结算 → 夜间普通书进货／里店秘密书上架二选一 → 夜结 → 次日”，并包含污染律令、35 天结局与重开。完整资产清单、逐按钮接口、调参和范围说明见 [控件蓝图闭环使用说明](Docs/控件蓝图闭环使用说明.md)。

营业页仍只显示表书店一层，不修改原图：`FirstFloorFrame` ScaleBox 位于 `(0,130)`、大小 `1920×900`，内层 SizeBox 为 `1440×675`；背景原图在裁剪 Canvas `FirstFloorScene`中放在 `(-240,-405)`，可见范围为 `x240..1680/y405..1080`。主角 `ShopkeeperGroup`仍在 `(905,200)`、大小 `160×225`，通过 `ShopkeeperFrame → ShopkeeperPortrait`显示男装 `T_HeroMale`，旧几何占位已移除。顾客组改为 `(661,283)`、大小 `320×300`，中心保持不变，使用 8 项 `CustomerPortraits`切换立绘；到场与点击人物后展开需求的流程保持不变。

三类书卡仍使用 `CardCanvas`和高 `96`的 `DescriptionScroll`，`ActionArea y=446`、`BtnPrimary`高 `48`、里店撤回按钮在动作区 `y=56`，本次不重建书卡。商人 `MerchantFrame → MerchantPortrait`已使用新增的专属 `T_Merchant`立绘。透明 `BtnMerchantHit`打开 `PurchasePanel`，关闭、结束夜晚及下次进入重置流程不变。新原图实际为白底 JPEG，原始字节保存在 `SourceArt/UI/Characters/Merchant/merchant_original.jpg`；内置 image_gen 抠图结果为 `merchant_cutout.png`，完整提示词及处理说明见该目录 `imagegen_record.json`。

在内容浏览器打开 `/Game/ProgramA/UI/Maps/L_BookstoreUI`运行。地图使用 `BP_UIGameMode`与 `BP_UIPlayerController`，后者的 **Root Widget Class**已指向 `WBP_UIRoot`；PlayerController负责创建、显示和注册根控件，不要再次 CreateWidget 或 RegisterView。GameMode已关闭首次自动开局，开场引导结束才调用 `RequestNewRun`。界面布局和连线可直接在 WBP 中修改；业务状态仍由唯一的 `ShopRunSubsystem`持有。

UI专用 `DT_RunRules_UI`启用 `bDaytimeOnlyLoop=true`：只有白天营业；秘密顾客根据已上架现货进入白天队列，第一天无上架秘密书时不会生成。夜结恢复 8 灵能，并在秘密书总拥有数不足 7 时合计补 1 册未上架副本；上架状态跨日保留。`DT_Decrees_UI`将四条不适用于新循环的代价改为明确资金消耗，具体对应关系见主说明，原 Release 表不变。夜间页面完成后调用 `RequestEndNight`，不再调用 `RequestOpenTableShop`。

本轮人物包 11 张 512×512 RGBA 原图完整保留在 `SourceArt/UI/Characters/Original/角色与猫头鹰立绘/`，不修改像素。导入 `/Game/ProgramA/UI/Art/Characters/`下 10 张纹理，实际使用 9 张：男装主角；普通 `IMG_6128/6130/6132/6133 → T_Normal01..04`；赶时间的人 `T_Hurry`；帽兜神秘人 `T_Secret`；污染 `IMG_6125/6127 → T_Polluted01/02`。女装 `T_HeroFemale`仅备用。包内猫头鹰不导入、不替换现有 `T_OwlGuide/WBP_OwlTutorial`。Brush 仅裁透明边距的 UV 并设置 ImageSize，ScaleBox 等比显示。 映射清单见 [character_manifest.json](SourceArt/UI/Characters/character_manifest.json)。后续换立绘需按新 PNG 透明边距同时更新 Brush UV 和 ImageSize；仅换 Texture 会沿用旧裁切，可能截断人物或出现额外留白。本次未修改原图像素。

`DT_RunRules_UI`已启用 `bUniqueDailyCustomerPortraits=true`。生成当天三人队伍时，按剩余可用种类的权重抽取，并在 `FCustomerRuntime.PortraitSlot`中固定保留不重复立绘；普通占槽 0–3、急躁 4、秘密 5、污染 6–7。受现有素材数量限制，急躁／秘密每天各最多 1 人，污染最多 2 人，普通补足三人；已接待者的立绘当天仍被占用。次日重新分配，允许不同日期出现同一人物。伪装顾客换入等待槽时也会避开当日已占用的普通立绘。`GetCustomerPortraitSlot`只读取记录，观察、拒绝、售卖或界面刷新不换脸、不额外消耗随机数。

**本次顾客去重与专属商人更新已验证：**Editor 编译及局部资产更新成功，43 项经营自动化通过；最终 `UniquePortraits_Merchant_UIVerify.log`为 38,134 项检查、0 失败、0 错误、0 警告，92 张实际渲染，包含 35 天与四结局回归。污染顾客两张图与帽兜神秘人均通过真实生成／按钮流程验证，特殊顾客截图使用明确标记的临时数据条件。当前污染顾客需污染 ≥61、购买普通书，额外交易污染配置为 0；秘密顾客需已上架秘密库存、出售基础加污染 10。当前共 11 张角色纹理（含备用女装），10 张实际使用，猫头鹰未改。本次只将 UI 规则中的去重开关置为 true，并原位替换商人 Brush；保留 `WBP_OwlTutorial/T_OwlGuide/DT_Decrees_UI`哈希。无需手工重建 WBP。

**历史 CharacterPortraits 版本：**34,240 项界面检查、85 张图，记录见 `Saved/Logs/CharacterPortraits_UIVerify.log`；当时四个保留资产哈希未变，41 项经营测试来自更早记录。共享 `Verification.txt`现已更新为本次 38,134 项结果。FirstFloorMerchant 的 29,746 项／77 张图和 CounterFlow 的 28,241 项／74 张图仍为历史。尚未进行人工 PIE 鼠标验收，未打包 exe。

本轮 CharacterPortraits 已重跑并通过 35 天与四结局真实按钮回归，结果与此前 FirstFloorMerchant／CounterFlow 相同：原始保存数据下，普通策略种子 731 得到 Cycle（资金 863、启蒙 0、污染 0）；进步书策略种子 731 启蒙 58 仍为 Cycle，种子 732 达到 Returned（资金 981、启蒙 74、污染 0）。原始数据连续拒客可在第 7 天关门（资金 -75）。污染释放仍在**明确的临时增强售卖污染边界场景**中经真实按钮链达到污染 100，不能据此宣称原始数值下的自然完整流程已测。关门、污染释放可早于第 35 天发生；结局页显示条件、最终数值和对应色条，并清空旧 Toast。

七条律令核心执行和弹窗可用，但 TTL／冷却状态展示及实际反噬播报尚缺；固定 3 客模式下无名律的客流惩罚不改变人数，闭架律额外阅读污染暂缺阅读入口，不能视为全部完成。历史继续延期，黑市、完整猫头鹰台词与存档不在本轮完成声明中。

本次已执行 `-run=ShopUIBuild -UpgradeUniquePortraits`（只开 UI 去重选项）和 `-UpgradeMerchantPortrait`（只替换商人 Brush）；备份在 `Saved/Backups/BeforeUniqueDailyPortraits_20261006_232341`。旧 `-UpgradeCharacterPortraits`会重建 SurfaceShop／Merchant／Root 及入口，`-UpgradeSceneLayout/-UpgradeCounterFlow/-Rebuild`也会覆盖各自范围的图表，**已有手工编辑后切勿直接重跑**。旧备份与迁移记录保留作历史，无需用户补做这些步骤。日常在 Designer／EventGraph 修改并 Compile、Save；复查使用 `ShopUIVerify`。

旧规则兼容说明：当 `bDaytimeOnlyLoop=false`时，底层仍保留旧版 `NightShop/NightSell`夜间表店交易和 `InsideCustomers`字段，以兼容旧表与测试；它们不是当前 UI 的玩法。`InsideSell`只保留枚举值，旧 Word 中里店经营的描述已过时。下文 Hello 示例以及旧版 29／36／38 项测试记录属于历史参考，当前状态以本节及主说明页首为准。旧版取消原生演示界面的验证记录见 [历史自动化结果](Saved/Automation/BlueprintUIOnly_20261006_Final/index.json)。

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
