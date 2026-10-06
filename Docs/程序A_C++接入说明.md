# 程序 A：C++ 逻辑与蓝图接入说明

> **当前入口：**`/Game/ProgramA/UI`下已接通 15 个具有真实 Designer 与 EventGraph 的可编辑 WBP，以及“菜单／引导 → 白天固定 3 槽（每人等 2–4 秒，点击到场人物后操作）→ 夜间进货／里店上架二选一 → 夜结／次日 → 律令、结局与重开”的接口闭环。请以 [控件蓝图闭环使用说明](控件蓝图闭环使用说明.md) 为当前接入与调参主说明，运行地图为 `/Game/ProgramA/UI/Maps/L_BookstoreUI`。当前使用 UI 专用规则及律令表，夜间不营业；原 Release 表和旧夜间表店模式保留兼容。

适用工程：`D:\unreal_project\Libary_release\Libary_Release.uproject`，引擎 UE 5.1，运行时模块 `Libary_Release`。**本次顾客去重／专属商人版本：Editor 编译、数据迁移和资产编译均成功；43 项经营自动化通过；最终界面回归 38,134 项检查、0 失败、0 错误、0 警告，92 张实际渲染。** 日志为 `Saved/Logs/UniquePortraits_Merchant_EditorBuild.log`、`UniquePortraits_DataBuild.log`、`UniquePortraits_Merchant_AssetBuild.log`、`UniquePortraits_Merchant_UIVerify.log`；经营报告在 `Saved/Automation/UniquePortraits_20261006/index.json`。35 天及四结局已回归；污染顾客与神秘人专项测试使用明确的临时生成配置。猫头鹰教程、猫头鹰纹理及 UI 律令表哈希未变，经营表仅开启立绘去重。旧 CharacterPortraits 的 34,240 项／85 图和此前验证仍属历史，共享 `Verification.txt`已更新。未进行人工 PIE 鼠标验收，未打包 exe。

**当前表店场景与立绘：**一层场景裁剪方式不变：`FirstFloorFrame (0,130,1920,900)`内 SizeBox 为 `1440×675`，原 `1920×1080`背景在裁剪 Canvas 中放在 `(-240,-405)`，对应原图 `x240..1680/y405..1080`。`ShopkeeperGroup (905,200,160,225)`内 `ShopkeeperFrame → ShopkeeperPortrait`显示男装 `T_HeroMale`，旧 `ShopkeeperPlaceholder`几何已移除。`CustomerGroup`改为 `(661,283,320,300)`，横向中心保持；`CustomerPortraits`为 8 项 WidgetSwitcher，子项依次为 `CustomerPortraitFrame0..7 → CustomerPortrait0..7`，人物与场景等比缩放。透明 `BtnPortraitHit`及选中后才显示需求的逻辑保留。

**人物映射与当日去重：**UI 经营表启用 `bUniqueDailyCustomerPortraits=true`，生成三人队伍时将互不重复的立绘保存在 `FCustomerRuntime.PortraitSlot`。普通槽 `0..3`、急躁 `4`、秘密 `5`、污染 `6..7`；每日急躁／秘密各最多 1 人、污染最多 2 人，其余由普通顾客补足。某种类的立绘耗尽后从其余合格种类按权重抽取。已服务者的立绘仍保留至当天结束，次日允许再次出现。Root.RefreshShop → ShopPage.RefreshPortrait → `GetCustomerPortraitSlot`只读取记录，不重新抽图；观察、售卖、拒绝均不换脸，伪装顾客换入等待槽时也避开已占用普通立绘。旧表默认关闭此选项，以兼容旧规则。

**人物素材：**`SourceArt/UI/Characters/Original/角色与猫头鹰立绘/`保留完整 11 张 512×512 RGBA PNG。导入 10 张纹理至 `/Game/ProgramA/UI/Art/Characters/`，9 张实际使用：`T_HeroMale`、普通 `IMG_6128/6130/6132/6133 → T_Normal01..04`、赶时间的人 `T_Hurry`、帽兜神秘人 `T_Secret`、污染 `IMG_6125/6127 → T_Polluted01/02`；`T_HeroFemale`已导入但仅备用。原图像素不改，Brush 只用 UV 裁透明边距和 ImageSize 定显示尺寸，外层 ScaleBox 等比。包内猫头鹰不导入，现 `T_OwlGuide/WBP_OwlTutorial`保持不变。 原图映射及清单见 [character_manifest.json](../SourceArt/UI/Characters/character_manifest.json)。未来换立绘需同步适配 Brush UV／ImageSize：不同 PNG 的透明边距不同，仅替换 Texture 会沿用旧裁切，可能截断人物或造成额外留白。按新图有效区域更新显示参数，不需修改原图像素。

**书卡布局：**三类 BookCard 使用 `CardCanvas`；正文 `DescriptionScroll`固定高 `96`，长文字可滚动。`ActionArea`固定 `y=446`，`BtnPrimary`高 `48`，里店 `BtnUnlist`位于动作区 `y=56`，使底部操作不受正文长度影响。上一版 FirstFloorMerchant 验证了 32 张实际嵌套卡的主按钮在整卡局部坐标中均为 `localY=462`（包含顶部内边距），长简介独立滚动。业务按钮的 `BookId`与原服务调用保持不变。

**商人页面：**`MerchantGroup`内 `MerchantFrame → MerchantPortrait`已改用专属 `T_Merchant`。原始白底 JPEG 和内置 image_gen 生成的透明 PNG 保存在 `SourceArt/UI/Characters/Merchant/`，处理记录及完整提示词见 `imagegen_record.json`；此为 AI 辅助抠图，原始文件另行保留。增加该纹理后共 11 张角色纹理、10 张实际使用。透明 `BtnMerchantHit`打开 `PurchasePanel`，`BtnClosePurchase`回场景，`BtnDone`始终能结束夜晚。Root.RefreshShop 仅对 `old!=6 && new==6`调用 ResetMerchant，购买刷新不关闭面板、再次进入重置。仅原位替换 Brush，按钮图表与布局保留。

**特殊顾客完成范围：**污染顾客达到污染 61 后可能进入表店，基础耐心 20 秒、购买普通书，可观察／拒绝／出售，使用 `T_Polluted01/02`；当前额外交易污染配置为 0，观察提示已改为实际行为描述。帽兜神秘人是 `Secret`，基础耐心 30 秒，只在有已上架秘密库存时可能生成；出售已上架秘密书按该书售价获得资金、基础污染 +10，使用 `T_Secret`。两类均已完成核心接待流程和立绘接入；秘密出售后的独立顾客转化剧情或回访系统不在当前实现中。

**到场 API：**UI 表启用 `bUseCustomerArrivalDelay=true`及 `CustomerArrivalMin/Max=2/4`。首位及后继顾客均由业务计时到场；读取 `IsCurrentCustomerPresent()`和快照 `bCustomerPresent/CustomerArrivalRemaining`，不要按已经生成的队列直接显示顾客。等待期间 UI 查询 `GetCurrentCustomerIndex=-1`，到场帧保留完整耐心；后续仅当前顾客计时。`NewDay/ResolveCustomer`重置界面选择，人物到场本身不会展开面板。

**本轮 CharacterPortraits 的 35 天与四结局证据：**本轮人物版本已重新通过，与此前 FirstFloorMerchant／CounterFlow 的种子及结果一致。真实 UI 保存数据经按钮流程完整经营 35 天，种子 731 普通策略为 Cycle（资金 863、启蒙 0、污染 0）；进步书策略种子 731 启蒙 58，正确保持 Cycle，种子 732 达到 Returned（资金 981、启蒙 74、污染 0）。原始数据拒绝顾客并支付房租可在第 7 天 Closed（资金 -75）。PollutionReleased 仍通过明确标记的临时增强售卖污染场景，经真实上架、到场和出售按钮达到污染 100；它不是原始污染数值下自然完整流程的验证。正式期限仍为 35 天，关门或污染极限可以提前结束，不生成第 36 天。结局页显示表中文案、触发条件、最终数值及色条，`ShowEnding`清空旧 Toast。等待／到场／选中、商人场景／采购、售书代表画面已经查看；采购图为 `merchant_purchase.png`，关闭图为 `merchant_closed.png`，均位于 `Saved/UIBuild/Preview/`。

**尚未全部完成：**七条律令核心执行及弹窗已接入，但 TTL／剩余冷却界面、实际漏洞反噬播报尚缺。固定 3 客模式下无名律的客流惩罚不改变人数；闭架律阅读额外污染暂缺阅读入口。本次没有进一步调整律令参数。历史、黑市完整内容和 P2 存档仍在已声明的延期或未纳入范围。

**当前人物迁移：**`-run=ShopUIBuild -UpgradeCharacterPortraits`只重建 SurfaceShop／Merchant／Root 及入口，不重建卡片／猫头鹰，不修改 DataTable；备份见 `Saved/Backups/BeforeCharacterPortraits_20261006`。旧 `-UpgradeSceneLayout/-UpgradeCounterFlow`及其备份保留作历史，不是换立绘需追加执行的步骤。用户手工编辑后切勿直接重跑这些参数或 `-Rebuild`覆盖成果，日常在 Designer／EventGraph 中修改并 Compile、Save。

**下文主体保留早期 C++ 接口说明和历史验证记录，发生差异时以上述页首及主说明为准。** 其中“手工创建全部界面”“项目全部引用 Release 表”“29／36／38 项测试”“首次自动开局”及旧夜间经营阶段，不能当作当前 UI 配置：当前入口已配置根 WBP，关闭首次自动开局，待引导结束调用 `RequestNewRun`；`RunRules/Decrees`引用 UI 专用表，其他六表继续使用 Release。旧 [Word 版接口指南](程序A逻辑完成情况与接口使用指南.docx)可供接口背景查阅，当前按钮和阶段流程优先依照主说明。

## 1. 哪些已写成 C++，哪些自动生成，哪些要在 UE 中连接

| 工作 | 当前范围 | 接入时要做什么 |
| --- | --- | --- |
| 经营、库存、随机顾客、污染、律令与结局 | C++ 已实现，规则由数据驱动；实际验证见文末 | UI 发命令并显示服务返回的状态，不再自行结算一遍。 |
| 原生类、接口、结构、枚举 | 编译时由 UE 反射系统注册 | 不用手建同名蓝图接口、结构或枚举。 |
| Release 八张 DataTable | 已由 `ShopReleaseData` 创建，独立进程重新加载校验通过，项目引用已切换 | 在项目设置核对八个软引用；日常改表使用编辑器，不重复运行创建命令覆盖。 |
| `ShopPlayerController` / `ShopGameMode` | C++ 类已写入，包含根控件生命周期和首次开局 | 手工派生两个蓝图、选根 WBP，并在需要的关卡指定 GameMode。工具不会自动改用户地图。 |
| 根 UI、按钮、库存列表、顾客、镇定与结局面板 | 现有程序 B 资产可继续使用，新接口接入待验收 | 根 WBP 实现原生 `ShopView`，将回调连到现有显示逻辑，将按钮连到服务请求。 |
| 历史事件 | 通用队列与见证接口保留；按用户决定暂缓接入 | Release 的 `bEnableHistory=false`，事件表为空，本轮不用制作或接通历史面板流程。 |
| 集市与夜枭 | 通用接口保留；正式内容与界面属于原程序 B 范围 | 本轮不宣称商品、110 条台词或 UI 验收完成；集市默认关闭。 |
| P2 存档 / 读档 | 尚未实现，本轮未明确纳入 | 当前运行状态只在 GameInstance 生命周期内保存，退出游戏不会自动存档。 |

运行时源文件位于 `Source/Libary_Release`，公开声明在 `Public`，实现在 `Private`。

| 文件 | 职责 |
| --- | --- |
| `ShopTypes.h` | 原生枚举、八种 DataTable 行结构、每册秘密书状态、运行快照与命令结果。 |
| `ShopService.h` / `ShopView.h` | UI 发请求与逻辑回传事件的原生接口。 |
| `ShopRunSubsystem.h/.cpp` | 唯一状态入口，阶段、日夜结算、结局、事务提交、计时和 UI 通知。 |
| `ShopPlayerController.h/.cpp` | 创建根 Widget、显示鼠标、设置 UI 输入、注册和移除视图。 |
| `ShopGameMode.h/.cpp` | 选择 C++ PlayerController；仅首次 `Boot` 自动开局。 |
| `ShopBlueprintLibrary.h/.cpp` | 蓝图 `Get Shop Service`、书籍类型显示文本。 |
| `ShopEconomy.h/.cpp` | 金钱、逐册库存、补货、收书、阅读、售书、启蒙概率与房租。 |
| `ShopCustomers.h/.cpp` | 顾客权重、分店资格、需求、队首操作及运行时耐心衰减。 |
| `ShopDecrees.h/.cpp` | 阶段、候选、可支付检查、持续期、漏洞、冷却和 `ShopEffects`。 |
| `ShopStory.h/.cpp` | 历史、集市、夜枭通用逻辑；内容接入范围见第 9 节。 |
| `ShopValidation.h/.cpp` | 校验行结构、范围、关联 ID、效果和结局配置。 |
| `ShopSettings.h/.cpp` | Project Settings → Game → Bookstore 的八张表软引用和随机种子。 |
| `Private/Tests/Shop*Tests.cpp` | `Bookstore.ProgramA` 自动化测试及本轮规则回归用例。 |

`Source/Libary_ReleaseEditor` 仅在编辑器目标使用：`ShopReleaseDataCommandlet` 生成本轮 Release 表；旧 `ShopBootstrapCommandlet` 保留 Prototype 检查入口。原有 `CppBridgeLibrary`、Hello 示例和 Prototype 备用资产继续保留。

## 2. 原生类型与 Release 八张表

完成 C++ 编译后重新打开编辑器，编译入口见 [README](../README.md)。UE 5.1 中打开 **Content Browser / 内容浏览器**，点右上角 **Settings / 设置**，勾选 **Show C++ Classes / 显示 C++ 类**；左侧展开 **C++ Classes → Libary_Release**。如果使用底部 Content Drawer，也可在其设置中打开同一选项。

`ShopRunSubsystem`、`ShopService`、`ShopView` 及结构、枚举均来自 C++。原生结构和枚举不一定显示成普通 `.uasset`；在变量类型、节点引脚和 DataTable 行结构选择器中查找即可。不要另建同名蓝图类型替代它们。

本轮八张表已生成在内容浏览器 **Content / ProgramA / Release / Data**，磁盘目录为 `Content/ProgramA/Release/Data`。项目设置的八个引用已切换至下表；备用 `Prototype` 未覆盖、未删除。

| Bookstore 设置项 | 资产路径 | 原生行结构 / 本轮内容 |
| --- | --- | --- |
| Books | `/Game/ProgramA/Release/Data/DT_Books` | `FBookData`；16 行。 |
| Customers | `/Game/ProgramA/Release/Data/DT_Customers` | `FCustomerData`；4 行。 |
| Run Rules | `/Game/ProgramA/Release/Data/DT_RunRules` | `FRunRules`；`Default` 行。 |
| Decrees | `/Game/ProgramA/Release/Data/DT_Decrees` | `FDecreeData`；7 行，均启用。 |
| Events | `/Game/ProgramA/Release/Data/DT_Events` | `FEventData`；空，历史暂缓。 |
| Market Items | `/Game/ProgramA/Release/Data/DT_MarketItems` | `FMarketItemData`；空，保留原程序 B 接入范围。 |
| Owl Lines | `/Game/ProgramA/Release/Data/DT_Owl` | `FOwlLine`；空，保留原程序 B 接入范围。 |
| Endings | `/Game/ProgramA/Release/Data/DT_Endings` | `FEndingData`；4 行。 |

打开 **Edit → Project Settings → Game → Bookstore**，核对八个资产选择框。完整软引用还带对象名，例如 `/Game/ProgramA/Release/Data/DT_Books.DT_Books`。`Config/DefaultGame.ini` 的八个引用已更新，Cook 目录已加入 `/Game/ProgramA/Release/Data`。后续更换数据目录时应同步修改这些配置。

`DT_RunRules` 的行名必须是 `Default`。请求中的 `BookId/DecreeId` 使用 **DataTable 行名**，不是中文显示名，例如 `book_novel`。有独立 `Id` 字段的表可留空，或填同一个行名。已经生成的表不需要重建；`ShopReleaseData` 有拒绝覆盖保护，后续只读检查使用 `-run=ShopReleaseData -VerifyOnly`。

## 3. UE 5.1 中的具体 UI 接入步骤

`UShopRunSubsystem` 继承 `UGameInstanceSubsystem`，UE 随 GameInstance 自动创建它。无需更换旧 GameInstance 蓝图、手动创建子系统或在各个控件里保存另一份权威状态。`Get Game Instance` 本身不能直接作为 `ShopService` 消息的 Target。

推荐由本轮新增的 C++ PlayerController 管理根 UI：

1. 打开程序 B 要使用的根 Widget Blueprint，在 **Class Settings → Implemented Interfaces → Add** 选择原生 **Shop View**，编译。按第 4 节实现接口事件，将事件连到现有界面的刷新与弹窗逻辑。
2. 在内容浏览器的 **C++ Classes / Libary_Release** 找到 `ShopPlayerController`，右键 **Create Blueprint Class Based on ShopPlayerController**，保存为例如 `BP_ShopPlayerController`。也可新建 Blueprint Class，展开 **All Classes** 搜索该类。
3. 打开这个蓝图，点 **Class Defaults**，在 Details 搜索 **Root Widget Class**，选择上一步的根 WBP，编译并保存。
4. 同样从 `ShopGameMode` 派生例如 `BP_ShopGameMode`。打开 **Class Defaults → Classes → Player Controller Class**，设为 `BP_ShopPlayerController`。默认 **Start New Run On First Entry** 开启，仅在状态为 `Boot` 时自动开局。
5. 打开需要接入的关卡，使用 **Window → World Settings** 打开世界设置，在 **GameMode Override** 选择 `BP_ShopGameMode`，保存关卡。此操作由接入者选择执行，本轮工具不会自动修改现有地图。也可由项目负责人在 **Project Settings → Maps & Modes** 配置默认 GameMode，但不需要两处重复设置。
6. 根 WBP 的 **Event On Initialized** 调用 **Get Shop Service**，用 **Is Valid** 检查有效并保存返回的 **Shop Run Subsystem Object Reference**。所有 `Request...` 和查询节点的 Target 都连接这个变量；子控件可从根 UI 获取同一个服务。不要把 GameInstance 或根 WBP 自己接作服务 Target。
7. C++ PlayerController 在 `BeginPlay` 中创建根 WBP、添加到视口并 `RegisterView`，退出时自动注销和移除。采用此路线后，删除或停用现有关卡蓝图、旧 PlayerController、根 WBP 中重复的 **Create Widget / Add to Viewport / Register View / Request New Run** 初始化连线，避免生成两套 UI 或重复开局。
8. 将按钮连接到第 5 节的请求，立即处理返回的 `bool`、结果枚举或命令结果结构；有错误文本则显示，没有文本时按结果枚举提示。点击 Play 后核对状态和事件回调，再逐项验收完整流程。

`RegisterView` 只接受实现原生 `ShopView` 的对象；原来的自建蓝图接口不会自动收到消息。切换根界面时可调用 PlayerController 的 `Attach Shop View`，由它完成旧视图注销、新视图注册；同样不要再重复手动注册。这两个函数的失败不会设置对应的 `GetLastError`，应检查对象有效性、是否实现原生接口，以及是否误在回调期间注册。

如果必须保留原 PlayerController，也可以采用旧的手动路线：只创建一次根 WBP → `Get Shop Service` → `Register View(Self)` → 按需开局，根视图销毁时 `Unregister View(Self)`。这条路线与上面的 C++ PlayerController 自动路线二选一，不能同时保留两套创建流程。

`RequestNewRun` 只允许 `Boot/End`。自动 GameMode 遇到正在进行的局不会重置，遇到 `End` 也不会自动重开；“重新开始”按钮可在 `End` 调用它。若保留主菜单的手动开始按钮，将 GameMode 的自动开局开关关闭。重新创建 UI 只应显示现有状态，不应无条件开新局。

## 4. ShopView 的实际事件参数

在根 Widget 的接口列表中实现对应事件。以下十个事件都没有返回值，但输入参数各不相同；不要统一改造成只有一个 Snapshot 的自建事件。

| 原生事件 | 输入参数 | UI 要做的事 |
| --- | --- | --- |
| `RefreshShop` | `Snapshot: FRunSnapshot` | 刷新天数、钱、灵能、污染、启蒙、阶段、当日收支等；需要顾客或单本库存时再查询服务。 |
| `ResolveCustomer` | `CustomerIndex: int32`，`Result: EShopActionResult` | 顾客已完成或离开时播放结果并更新队列；不消费顾客的失败不会发此回调。 |
| `NewDay` | `Day: int32` | 新开局或天数变化时更新营业日标题；注册视图不会补发，首次显示要读取 `RefreshShop` 的 Day。 |
| `OpenCalmPanel` | `Candidates: TArray<FName>`，`Snapshot: FRunSnapshot` | 打开镇定界面，用候选 ID 查询律令详情。 |
| `ShowDecreeResult` | `DecreeId: FName`，`Result: FShopCommandResult` | 显示施行流程的结果；成本不足、不可用、阶段错误等直接返回失败，不保证发此回调，须处理请求返回值。 |
| `ShowObserveResult` | `CustomerIndex: int32`，`Customer: FCustomerRuntime` | 显示当前顾客的观察结果。 |
| `ShowHistoryPanel` | `EventId: FName`，`Event: FEventData` | 显示历史文本与当前支持的见证按钮。 |
| `ShowEnding` | `Ending: EShopEnding`，`Snapshot: FRunSnapshot` | 显示关门、污染释放、归还或循环结局；用 GetEndingInfo 读取结局表的标题与文本。 |
| `ShowOwlTip` | `LineId: FName`，`Text: FText` | 显示已配置的夜枭台词。 |
| `OpenMarket` | `ItemIds: TArray<FName>`，`Snapshot: FRunSnapshot` | 用商品 ID 查询详情并生成集市条目。 |

回调中只更新界面和读取快照。C++ 在通知期间会拒绝重入的修改命令；购买、颁布律令等请求应由后续用户点击发起。回调在请求函数返回前同步执行，注册视图时也会立即刷新；具体接线注意事项见第 5 节。

也可以绑定子系统的 `OnMoneyChanged`、`OnInventoryChanged`、`OnCustomersChanged`、`OnPollutionChanged` 等事件后主动查询。根 UI 用 `ShopView` 接入通常更直观，无需为同一次操作同时维护两套业务状态。

## 5. UI 可以调用的接口

### 原有 11 个接口

原有名称和返回形式保留，但调用权限由新的完整阶段流程决定。

| 接口 | 参数、返回值 | 使用方式 |
| --- | --- | --- |
| `RequestNewRun` | 无参数 → `bool` | 仅 `Boot/End` 开始新一局。 |
| `GetSnapshot` | 无参数 → `FRunSnapshot` | 获取只读状态副本。 |
| `GetCustomers` | 无参数 → `FCustomerRuntime` 数组 | 表店当前营业批次的顾客队列；里店管理期间可保留暂停的夜间队列，不能显示为里店顾客。只操作第一位 `bServed=false` 的顾客。 |
| `GetBookInfo` | `BookId` → `bool`，输出 `BookData`、`Stock` | 获取配置和已经拥有的库存；里书店可收取数量另见 `GetBookRuntime`。 |
| `RequestBeginSell` | `CustomerIndex` → `EShopActionResult` | `Day/NightShop` 选择队首；`Opened` 后进入 `Sell/NightSell`，都在表店。 |
| `RequestSell` | `BookId` → `EShopActionResult` | 在选书阶段提交书籍。`Sold` 才有收入、扣库存；错误结果要按枚举处理。 |
| `RequestCancelSell` | 无参数 → `bool` | 退出选书回到 `Day/NightShop`，不算完成该顾客。 |
| `RequestEndDay` | 无参数 → `bool` | 从 `Day` 到 `DayEnd`；是否允许提前关门由配置控制。 |
| `RequestContinue` | 无参数 → `bool` | 兼容“继续”按钮：日结后进入黄昏选择；夜间活动后结算；夜结后进入集市或次日；集市中则关闭集市。 |
| `RequestRestock` | `BookId` → `bool` | 仅 `Restock` 购买一本表世界书，立即扣成本和记支出。 |
| `RequestNextDay` | 无参数 → `bool` | 仅夜间结算完成且无未处理集市时进入次日；最后一天转结局。 |

选择错误书籍不改钱和库存。默认 `bWrongBookConsumesCustomer=true`，错误选择仍会让顾客离开；没有匹配库存时是否离开由 `bNoMatchConsumesCustomer` 控制。因此 `WrongBook/NoMatch` 虽然不是交易成功，也可能已经推进顾客队列，UI 应以回调和新快照为准。

顾客索引从 `0` 开始。`GetCustomers` 返回的数组含已服务记录，找队首时取第一个 `bServed=false` 的数组索引。`GetActiveCustomerIndex=-1` 表示当前未打开选书，不表示没有排队顾客。顾客耐心由 C++ 自动推进，只有队首计时；UI 不要再写第二套倒计时扣减逻辑。

### 新增命令

以下接口统一返回 `FShopCommandResult`。

| 接口 | 输入参数 | 有效场景或结果 |
| --- | --- | --- |
| `RequestObserveCustomer` | `CustomerIndex` | 营业或选书时观察队首，标记已观察并回调 `ShowObserveResult`。 |
| `RequestRejectCustomer` | `CustomerIndex` | 营业或选书时拒绝队首，让队列前进。 |
| `RequestOpenInside` | 无 | `DuskChoice` 选择里店管理；或当晚已选择里店时从 `NightShop` 返回。没有里店顾客。 |
| `RequestOpenTableShop` | 无 | 从 `Inside/Restock` 到 `NightShop`，同夜只生成一次表店顾客队列。 |
| `RequestListSecretBook` | `BookId` | `Inside` 上架一册秘密书，不收费、不增污、不增加总库存。 |
| `RequestUnlistSecretBook` | `BookId` | `Inside` 撤回一册上架书，保留副本标记。 |
| `RequestOpenRestock` | 无 | `DuskChoice` 选择本晚采购表世界书籍。 |
| `RequestCollectSecret` | `BookId` | `Inside` 消耗灵能，从有限的可收取数量中取得一本秘密书。 |
| `RequestReadSecret` | `BookId` | `Inside` 阅读一册尚未读过的秘密书，结算收益与该副本的额外污染；历史本轮关闭。 |
| `RequestEndNight` | 无 | `Inside/Restock/NightShop` 完成本晚结算；选书期间需先取消选书。 |
| `RequestPurify` | `Amount: int32` | 轻度污染的 `Calm` 面板，用等量灵能降低污染；数量须为正且不超过现有污染、灵能。 |
| `RequestEnactDecree` | `DecreeId` | `Calm` 颁布本次候选中的可用律令，检查成本、生效和冷却状态。 |
| `RequestSkipDecree` | 无 | 关闭当前 `Calm` 请求并恢复原阶段。 |
| `RequestHistoryChoice` | `Choice: EHistoryChoice` | `History` 处理当前事件；枚举只支持 `Witness`，本轮历史开关关闭，暂不接入。 |
| `RequestOwlTalk` | 无 | 有效游戏中、非 `Calm/History` 时请求台词；缺失台词返回 `Unavailable`。 |
| `RequestOpenMarket` | 无 | 启用集市时，在每周末的 `NightEnd` 打开当周集市。 |
| `RequestBuyMarketItem` | `Id` | `Market` 购买当前商品，价格、禁忌代价、效果、副作用一起结算，失败不扣除部分资源。 |
| `RequestCloseMarket` | 无 | 关闭本晚集市，直接进入次日；最后一天则进入结局。 |

用蓝图 **Break Shop Command Result** 读取：

- `bSucceeded`：本次请求是否成功。
- `Code`：`Success`、`Sold`、`InvalidPhase`、`Unavailable` 等结果枚举，用于分支。
- `Message`：失败原因等显示文本。成功时可能为空。
- `SubjectId`：本次涉及的书籍、律令、事件或商品 ID。

修改命令返回 `false` 或失败枚举时，在同一次按钮执行链中立即读取并保存 `GetLastError()` 和 `GetLastResult()`；返回 `FShopCommandResult` 的请求直接保存返回结构。自动耐心 Tick 也会提交 `Success` 并覆盖最近结果，因此不能延迟到下一帧再读。查询函数不更新最近命令结果，查询 `false` 不应显示上一条命令的错误。

其他蓝图查询有 `GetBookIds`、`GetBookRuntime(BookId)`、`GetDecreeInfo(Id)`、`GetMarketItemInfo(Id)`、`GetEventInfo(Id)`、`GetEndingInfo(Ending)`、`CanEnactDecree(Id, out Reason)`、`BuildCustomerNeedText(CustomerIndex)` 和 `HasMatchingStock(Type, Layer)`。`CanEnactDecree` 返回 `bool` 及不可施行原因，可用于镇定面板按钮禁用与提示；阶段不是 `Calm` 时也会返回 `false`。

`FBookRuntime.Stock` 是拥有量，`AvailableToCollect` 是本晚剩余可收取量，`ReadCopies` 是已读册数；秘密书的 `SecretCopies` 保存每册的已读、封存、篡改、污染状态。UI 查询这些副本即可，不应自行改写库存。`GetDecreeInfo` 也支持运行时保底项 `emergency_calm`，无需额外添加第八条律令数据。

### 返回值与回调的八条接线注意

1. **先用快照初始化，再等阶段事件。** `RegisterView` 成功时立即发送 `RefreshShop`，并补发当前镇定、历史、集市或结局面板；不会补发 `NewDay`。根 UI 可能先收到 `Boot` 快照，再收到自动开局的刷新。
2. **律令失败不只靠回调。** `ShowDecreeResult` 不能替代 `RequestEnactDecree` 的同步返回值；普通失败分支直接返回错误，不发该回调。
3. **交易失败不等于顾客没离开。** `WrongBook/NoMatch` 是否消费顾客取决于配置，消费时才发 `ResolveCustomer`；`OutOfStock` 不发。消费型失败的 `Message` 可能为空，应按 `Code` 显示“选错书”或“无匹配库存”，并刷新队列。
4. **错误在当前执行链中保存。** 不使用 Delay 后再读 `GetLastResult`；自动 Tick 可能已覆盖结果。对结构返回值直接 Break 后保存。查询和视图注册失败则检查各自返回值，不套用旧命令错误。
5. **律令按钮先查询可施行性。** 用 `CanEnactDecree(Id, out Reason)` 设置按钮状态，显示原因；点击后仍须处理实际请求返回值，不能由 UI 自己扣成本。
6. **刷新时复用控件。** `RefreshShop`、`OnInventoryChanged`、`OnCustomersChanged` 随每次计时提交触发，可能每帧调用。更新已有文本、条目和耐心条，不要每次重建根 WBP 或整个列表。
7. **回调中不发修改命令。** 回调同步发生在请求返回前；`RefreshShop/OpenCalmPanel` 内只显示和查询。后续用户点击才可购买、颁布或结束一天，避免被重入保护拒绝。
8. **重新获取顾客索引和最终阶段。** 进入里店或插入假顾客后重新取得数组，避免缓存旧索引。成功售书、阅读或夜结也可能转入 `Calm/End`，按钮与面板以最新 `Snapshot.Phase` 为准，不能强行切回预想阶段。

默认由项目设置加载配置。需要测试另一组表时，可在 `Boot/End` 调用 `ConfigureTables(Books, Customers, Rules, Decrees, Events, Market, Owl, Seed, Endings)`，再调用 `ValidateConfig` 查看输出错误。进行中的一局不能替换配置；编辑了 DataTable 后，重新启动 PIE 会重新加载，单独点击“新一局”使用的是当前已加载的配置副本。

`RandomSeed=-1` 每局使用新种子；填非负固定种子便于复现顾客和经营随机结果。夜枭随机闲聊使用独立的 `CosmeticRandom`，点击闲聊不会改变后续顾客、候选或经营事件的随机结果。

## 6. 阶段与按钮

```text
Boot --RequestNewRun--> Day <--> Sell
                         |
                  RequestEndDay
                         v
                       DayEnd
                         |
                  RequestContinue
                         v
                    DuskChoice
                  /            \
       RequestOpenInside      RequestOpenRestock
                v                v
       Inside <--> InsideSell  Restock
                \                /
                    RequestEndNight
                          v
                       NightEnd
                    /           \
     启用集市且当周末             无集市待处理
           v                          |
        Market                 RequestNextDay
           |                          |
    RequestCloseMarket          次日 Day / End
           |
      次日 Day / End
```

一晚只能选择 `Inside` 或 `Restock`，不能在同一晚切换。白天顾客处理完后仍由 UI 调用 `RequestEndDay`；顾客为空并不表示整局结束。Release 默认关闭集市，因此正常夜结后走次日分支。

`Calm` 会中断当前流程并暂停顾客计时；净化、颁布或跳过后由 C++ 恢复原阶段。`History` 保留同样的中断恢复能力，但本轮关闭。UI 不应自己调用“下一天”来关闭这些面板。第 35 天完成夜结及必要的集市后进入结局，不创建第 36 天；关门或污染极限可提前结束。

## 7. 本轮已确定的经营、库存与顾客规则

下表是 Release 数据与实现约定；原来 Prototype 的起始灵能 24、两名夜间顾客及仅部分律令启用等样例不再代表本轮运行配置。改数值优先改表，再重新加载配置。

| 项目 | 当前执行规则 |
| --- | --- |
| 基本经营 | 初始金钱 100，期限 35 天，房租基础 25；`BeforeDusk` 在结束白天时扣租，也可配置 `NightEnd`。同一天只扣一次。 |
| 负债 | 不因一次负余额立即关门；连续 3 次夜结余额为负触发关门，恢复非负会清零连续计数。 |
| 灵能 | 初始 0，上限 100。秘密书已在开局拥有，可先阅读获得灵能。收取新书默认每本消耗 12 灵能。 |
| 初始库存 | 所有书的 `InitialStock` 都是已拥有库存。旧 `InitialOwnedStock` 仅为兼容保留，不再额外相加。初始秘密书每册已封存、未读。 |
| 每晚收书 | `CollectOfferPerNight` 与已拥有量分开；Release 每种秘密书每晚提供 1 册，进入里店时刷新，当晚收取后减少。新收取副本封存且未读。 |
| 每册状态 | `SecretCopies` 记录 `bRead/bSealed/bAltered/bPolluted/bListedForSale`；上架不改变其他标记。`Stock/ReadCopies/ListedCopies/StoredCopies` 是汇总。售卖仅选择已上架副本，在其中优先移除已读副本。 |
| 基础收益和污染 | 阅读使用该书 `PsychicYield/PollutionYield`；值为 -1 才回退至全局阅读值。收书默认污染 +5，秘密书售卖默认 +10，分别使用全局配置，不被阅读污染覆盖。 |
| 进步书启蒙 | `book_novel_03`、`book_history_02`、`book_history_03` 每次成功售出独立以 50% 概率获得启蒙 +2，失败交易不抽奖、不加启蒙。其他书默认没有该售卖奖励。 |
| 额外污染 | 被篡改的副本阅读额外 +2；潮汐返架的污染副本阅读或售卖额外 +3，可与基础污染相加。 |
| 收入与灵能倍率 | `IncomeMultiplier` 作用于秘密书收益；`NightIncomeMultiplier` 作用于夜间表店所有成交。秘密书夜售合并两者后向下取整；不回扣已取得收入。 |
| 白天表店顾客 | 出现 `Normal/Hurry`，以及符合污染门槛的 `Polluted`；需求为小说、诗集或历史。秘密顾客不在白天生成。白天基础客流3～5，第二周起增加1。 |
| 夜间表店顾客 | 四种角色均可出现，权重普通5、急躁3、秘密2、污染2；污染顾客仍要求至少61。每晚基础客流4，不保证每类各一人。只有Secret需求为秘密书，其余角色需求普通书。所有人都在表店；里店没有营业顾客。 |
| 耐心 | 初始显示完整耐心（普通 30 秒、急躁 15 秒、秘密 30 秒、污染 20 秒）；当前污染达到 61 后，所有顾客按 `DeltaSeconds × 1.3` 消耗。污染降低后恢复正常速率，旧的生成时乘 0.7 规则停用。只有队首计时。 |
| 客流代价 | 闭门律的-1作用当晚表店；队列已生成时减少一个未服务、非当前选书且非假顾客。尚未生成时在夜间生成时扣减；同夜往返不会重扣。无名律永久客流惩罚作用白天和夜间表店。 |

污染阶段阈值为轻度 31、中度 61、重度 86，终止上限 100。一次命令中污染达到终止上限会留下标记，即便后续减污也不能撤销已经触发的终止条件。增污统一经过 `ShopEffects::ChangePollution`，因此“下一次污染 +5”等效果不会遗漏正常业务入口。

每次命令先在运行状态的副本上计算，整笔成功后提交；支付不足、无库存、无效阶段等失败不会留下部分扣款或消耗随机序列。UI 获取的快照也是只读副本，不能作为另一套独立经营状态。

## 8. 律令的持续期、候选和七条具体效果

“2 回合”指两个**逻辑结算点**，不再固定解释为两晚。每次夜结推进一个点；本轮 `bAdvanceTurnOnStageRise=true`，污染跨入更高阶段也推进一个点。同一次动作即使跨多个阈值，最多追加一个阶段结算点，漏洞效果不会递归推进。普通点击和真实时间流逝本身不推进回合。漏洞到期后开始 3 个逻辑点的冷却；生效中或冷却中不可重复施行。兼容的 `bStageCrossTriggersLoophole=false` 不表示本轮关闭上述阶段回合推进。

每次打开镇定面板都重新抽候选：轻度最多 3 项、中度和重度最多 4 项；先按阶段、启用、生效、冷却和**完整可支付成本**过滤，再抽取。候选不足不会补重复项。重度有 1 个逻辑点的处置宽限，未满足处置条件的额外污染按配置结算。

没有可支付的普通律令时显示 `emergency_calm`“应急镇定”：金钱 -20、污染 -5，允许负债，不消耗灵能，也不生成持续期、漏洞或冷却。它是运行时保底项；正式律令表仍只有七条。

| 行名 / 律令 | 生效与代价 | 漏洞和本轮细则 |
| --- | --- | --- |
| `bronze_01` 静阅律 | 灵能8、污染-15，当晚表店后续售书收入×0.8。 | 到期挂起一次“下次正向污染+5”，零增量或减污不消费。取消夜间营业后的收入代价适配尚待修改。 |
| `bronze_02` 闭门律 | 灵能8，阻止轻度扩散；当晚表店顾客-1。 | 轻度扩散旧规则保留。漏洞假顾客仅在表店营业/选书时加入队列；在管理页触发则等待回表店。假顾客不可交易，可观察/拒绝，等待污染仍受每名上限约束。 |
| `bronze_03` 燃烛律 | 灵能 8、污染 -10，持续期间自然衰减 ×2、阅读灵能收益 ×0.8。 | 漏洞跳过一次夜间自然衰减；若在夜结开始时到期，则影响当次夜结。不是直接额外污染 +5。 |
| `silver_01` 闭架律 | 灵能 18、污染 -30，随机一册已拥有秘密书被篡改，之后阅读额外污染 +2。 | 漏洞只从仍在库存且同时处于篡改、封存状态的副本中解封一册，并独立污染 +10；对应副本已售出、被焚毁或没有符合条件的副本时不解封其他书，+10 仍照常执行。篡改与封存按册记录，阅读仍允许读取封存本。 |
| `silver_02` 守夜律 | 灵能 18，持续期间自然衰减 ×2；每晚金钱 -30 为本局永久代价。 | 漏洞后每夜污染 +3 为本局永久。相同律令来源、相同永久效果重复出现不会叠加多份。 |
| `gold_01` 无名律 | 灵能 35、污染 -50，删除一条历史线索。 | 无线索时不可支付，因此不会进入普通候选。漏洞永久增加房租 10（累计最多 30）或减少客流 1（累计最多 2）；两项都可增加时等概率，达到上限的项让给另一项。 |
| `gold_02` 潮汐律 | 灵能 35、污染 -50，随机毁去 2 册已拥有秘密书，并重置阶段提醒记录。 | 出售和销毁秘密书都会进入失去书籍池；漏洞从池中返还 1 册未封存、未读的污染书，阅读或售卖额外 +3；池空则污染 +5。“重置阈值”重置的是阶段提醒状态，数值 31/61/86/100 不变。 |

七条律令均已启用，`BlockLightSpread` 已有明确规则和实现，不再因“扩散未定义”被拒绝。启用不代表当前一定可用，例如 Release 历史关闭且默认线索掉落概率为 0 时，无名律通常因无线索而不可支付。

数值字段和文字字段的作用不同：`EffectText/CostText/LoopholeText/Text` 用于显示；真正执行的是 `PsychicCost/PollutionCut` 和结构化 `CostEffect/Effects/LoopholeEffect`。只把文字写成“收入减半”不会改变收入，必须同步配置对应倍率。`FShopEffect.Amount` 的资源值有正负号；`DurationTurns=0` 使用所属效果默认时效，`-1` 表示本局永久，正数表示逻辑点数。当晚收入/客流代价在当晚结算后清除。

## 9. 数据来源、结局与仍未纳入的内容

Release 的 16 本书来自已提供的《数值调参表》书籍表，包含 9 本表书和 7 本秘密书，并非新增编造书籍。三本进步书的售卖奖励采用本轮确认的 50% 概率。顾客表使用四类配置，需求句支持 `{类型}`，也兼容 `{BookType}`、`{NeedType}`、`{Type}`、`{Name}` 占位符。

四个结局由 `DT_Endings` 驱动，含条件、优先级、标题和显示文本。当前优先级依次是关门、污染释放、归还、守旧循环；关门要求连续 3 次夜结负余额，污染释放阈值为 100，最终归还要求启蒙至少 60 且污染低于 60，否则进入循环。归还默认不要求金钱达到 1500；有需要时通过结局行的 `bRequireMoney/MinMoney` 设置，不能只修改旧的显示目标。UI 收到 `ShowEnding` 后调用 `GetEndingInfo` 读取对应文案。已有《剧情设定与分幕剧本 V7.0》提供剧情及结局文本，本轮表内采用四个结局的短说明，不声称完整演出已接入。

历史按用户决定暂缓，`bEnableHistory=false`，`DT_Events` 留空。剧本文件和女孩残页等内容已经提供；暂缓不表示缺少剧情文件，也不表示女孩见证的启蒙和灵能奖励已接入当前流程。

集市和夜枭仍按原程序 B 范围对接。本轮保留了交易、禁忌代价、台词顺序与随机等 C++ 接口，但 Release 的两张内容表留空，`bEnableMarket=false`。已有调参表包含 12 件商品；这里的空表不表示策划未提供商品。夜枭接口需要 `GuideIndex=1..10` 的顺序引导及 `GuideIndex=0` 的随机闲聊；缺数据返回 `Unavailable`。本轮没有宣称 12 件商品或 110 条台词已完成正式导入、界面接线和验收。

Release 启用核心内容数量校验，尚未启用包含程序 B 内容量的最终数量校验。P2 存档/读档未实现，本轮没有明确追加为任务。议价规则尚未定义，未编造议价算法。后续补内容优先编辑对应 DataTable，并通过配置校验；不要另造并行的 C++ 状态或 UI 结算逻辑。

## 10. 检查与回归入口

以下是运行入口，具体结果以当次进程、日志和报告为准。关闭占用工程的编辑器后，在 PowerShell 中执行。

只读检查磁盘上的 Release 八张表：

```powershell
& 'D:\epic\UE_5.1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\unreal_project\Libary_release\Libary_Release.uproject' -run=ShopReleaseData -VerifyOnly -unattended -NullRHI -nosplash -log
```

旧 Prototype 的检查入口仍是 `-run=ShopBootstrap -VerifyOnly`。这两个命令都只用于检查各自的目录；不要把 Prototype 校验结果当成 Release 校验结果，也不要重新生成覆盖策划编辑过的表。

运行程序 A 自动化测试组：

```powershell
& 'D:\epic\UE_5.1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\unreal_project\Libary_release\Libary_Release.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests Bookstore.ProgramA;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\unreal_project\Libary_release\Saved\Automation\ProgramA' -log
```

UE 5.1 保留上面的 `;Quit`，它是 Automation 队列的退出子命令；仅写 `-TestExit` 可能在报告导出后仍不退出。

UI 最短验收链路：所选 GameMode / PlayerController 启动 → 根 WBP 只创建一次并收到刷新 → `Get Shop Service` → 查询顾客 → 选择队首 → 售书 → `RefreshShop/ResolveCustomer` 更新 → 日结 → 黄昏互斥选择 → 夜结 → 次日。再验证逐册阅读、污染跨阶段弹窗、候选不足保底及结局显示。集市、夜枭和历史按各自接入范围另行验收。

## 本轮实际验证记录

2026-10-06 本轮实际结果如下。自动化报告生成时间为 `2026.10.06-10.31.34`，以本轮最终报告为准。

| 验证项 | 结果 |
| --- | --- |
| `Libary_Release Win64 Development` | 完整编译、链接成功，退出码 0。日志：[ProgramA_Usage_GameBuild_Final.log](../Saved/Logs/ProgramA_Usage_GameBuild_Final.log)。 |
| `Libary_ReleaseEditor Win64 Development` | 完整编译、链接成功，退出码 0。日志：[ProgramA_Usage_EditorBuild_Final.log](../Saved/Logs/ProgramA_Usage_EditorBuild_Final.log)。 |
| `Bookstore.ProgramA` 自动化测试 | 29 项全部成功；`succeeded=29`、`succeededWithWarnings=0`、`failed=0`、`notRun=0`、`inProcess=0`。报告：[index.json](程序A测试报告_20261006.json)；日志：[ProgramA_Usage_Tests_Final.log](../Saved/Logs/ProgramA_Usage_Tests_Final.log)。 |
| Release 八表 | 已实际创建全部八个 `.uasset`；独立新进程 `-run=ShopReleaseData -VerifyOnly` 重新加载并校验成功。日志：[生成](../Saved/Logs/ProgramA_Usage_CreateData.log)、[独立校验](../Saved/Logs/ProgramA_Usage_VerifyData.log)。 |
| 项目数据引用 | `DefaultGame.ini` 八个表引用已全部切换至 Release，Cook 目录已加入 `/Game/ProgramA/Release/Data`，Prototype 备用目录保留。真实项目设置初始化路径已纳入第 29 项回归。 |
| Git LFS | `.gitattributes` 与本地 LFS hooks 已配置；没有迁移既有提交历史，也没有提交或推送本轮变更。 |
| UI、地图和包体 | 新 C++ GameMode/PlayerController 已编译；人工界面连接、关卡指定、PIE 试玩与 Windows 打包尚未验收。 |

第 29 项测试为 `Bookstore.ProgramA.ReleaseFlow.ProjectSettingsInitialization`，覆盖从项目设置加载八张 Release 表并初始化服务。原有 `ConfigRejectsInvalidData` 还增加了 `UnlockSecretBook` 负数量和 `ReturnLostSecretBook` 非法目标的校验断言；相关源代码修复已通过本轮最终回归。其余测试覆盖初始已拥有库存、逐册读售、50% 售卖随机、昼夜顾客池、律令生命周期及反噬、假顾客和 35 天结局等逻辑。

剩余验收是按第 3 节连接根 WBP、指定关卡 GameMode，进行 PIE 人工试玩及 Windows 打包。上述 C++、数据和自动化结果不能替代 UI 点击、画面表现或包体验收；历史、集市、夜枭与 P2 存档的范围仍按第 9 节执行。
