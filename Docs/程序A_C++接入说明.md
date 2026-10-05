# 程序 A：C++ 逻辑与蓝图接入说明

适用工程：`D:\unreal_project\Libary_release\Libary_Release.uproject`，引擎 UE 5.1，运行时模块 `Libary_Release`。

当前交付是经营逻辑、数据结构、蓝图接口和 Prototype 数据表。程序 B 现有的界面还没有接入这些接口，因此不能把本次交付理解为已经可以直接游玩的完整游戏。下面说明如何接线，以及哪些内容仍需策划确认。

本次验证记录（2026-10-05）：UE 5.1.1 的 `Libary_ReleaseEditor Win64 Development` 和 `Libary_Release Win64 Development` 均编译成功；14 项 `Bookstore.ProgramA` 自动化测试全部通过，0 失败，测试进程退出码为 0。七张 Prototype 表生成后，在另一个进程中从磁盘重新加载并通过配置校验。报告位于 `Saved/Automation/ProgramA_Final/index.json`，日志位于 `Saved/Logs/ProgramA_Final.log`、`ShopBootstrapVerify.log` 和 `ProgramA_GameBuild.log`。此记录覆盖 C++ 业务与数据加载，未将程序 B 的现有界面接线、PIE 人工游玩或 Cook/完整打包列为已完成。

## 1. 文件分别负责什么

运行时源文件位于 `Source/Libary_Release`，公开声明在 `Public`，实现在 `Private`。

| 文件 | 职责 |
| --- | --- |
| `ShopTypes.h` | 原生枚举、DataTable 行结构、运行快照、命令结果与内部状态。 |
| `ShopService.h` | UI 向逻辑发请求的原生接口，包括原有 11 个接口和新增命令。 |
| `ShopView.h` | 逻辑向 UI 回传刷新、弹窗、顾客处理结果的原生接口。 |
| `ShopRunSubsystem.h/.cpp` | 唯一状态入口，管理阶段、日夜结算、结局、事务提交和 UI 通知。 |
| `ShopBlueprintLibrary.h/.cpp` | 提供蓝图节点 `Get Shop Service`、书籍类型显示文本。 |
| `ShopEconomy.h/.cpp` | 金钱、库存、补货、收书、逐册阅读、售书和房租结算。 |
| `ShopCustomers.h/.cpp` | 随机顾客、需求、队首操作、耐心倒计时和需求文本。 |
| `ShopDecrees.h/.cpp` | 污染阶段、律令候选、生效与漏洞；`ShopEffects` 执行结构化效果。 |
| `ShopStory.h/.cpp` | 历史事件队列、见证、每周集市和夜枭台词。 |
| `ShopValidation.h/.cpp` | 开局前检查数据类型、数值范围、关联 ID 与未支持的效果。 |
| `ShopSettings.h/.cpp` | Project Settings → Game → Bookstore 中的七张数据表软引用和随机种子。 |
| `Private/Tests/ShopRunTests.cpp` | `Bookstore.ProgramA` 自动化测试入口。 |

`Source/Libary_ReleaseEditor` 是编辑器专用模块，其中 `ShopBootstrapCommandlet` 负责创建或只读检查 Prototype 表。原来的 `CppBridgeLibrary` 和 Hello 示例继续保留。

## 2. 先确认原生类型和七张表

完成 C++ 编译后重新打开编辑器。内容浏览器开启 **Show C++ Classes / 显示 C++ 类**，可在 `C++ Classes / Libary_Release` 下找到原生类。编译步骤仍见 [README](../README.md)。

`ShopRunSubsystem`、`ShopService`、`ShopView` 以及 `BookData`、`RunRules` 等结构和枚举均由 C++ 定义。不要再手建同名蓝图接口、结构体或枚举；名称相似的蓝图结构也不能代替 DataTable 要求的原生行结构。原生结构和枚举不一定各自显示为普通 `.uasset`，可在变量类型、节点引脚和 DataTable 行结构选择器中查找它们。

Prototype 资产位于内容浏览器的 `Content / ProgramA / Prototype / Data`，磁盘位置为 `Content/ProgramA/Prototype/Data`。

| Bookstore 设置项 | 资产路径 | 原生行结构 |
| --- | --- | --- |
| Books | `/Game/ProgramA/Prototype/Data/DT_Books` | `FBookData` / BookData |
| Customers | `/Game/ProgramA/Prototype/Data/DT_Customers` | `FCustomerData` / CustomerData |
| Run Rules | `/Game/ProgramA/Prototype/Data/DT_RunRules` | `FRunRules` / RunRules |
| Decrees | `/Game/ProgramA/Prototype/Data/DT_Decrees` | `FDecreeData` / DecreeData |
| Events | `/Game/ProgramA/Prototype/Data/DT_Events` | `FEventData` / EventData |
| Market Items | `/Game/ProgramA/Prototype/Data/DT_MarketItems` | `FMarketItemData` / MarketItemData |
| Owl Lines | `/Game/ProgramA/Prototype/Data/DT_Owl` | `FOwlLine` / OwlLine |

在 **Edit → Project Settings → Game → Bookstore** 核对这七个引用。软引用的完整对象路径会额外带资产名，例如 `/Game/ProgramA/Prototype/Data/DT_Books.DT_Books`；在资产选择器中直接选择对应表即可。

这些引用同时写入了 `Config/DefaultGame.ini`。Prototype 数据目录也加入了打包时必须 Cook 的目录，避免仅由设置加载的表被遗漏；以后更换为正式数据目录时，应同步检查对应 Cook 配置。

`DT_RunRules` 必须保留名为 `Default` 的行。书籍、律令、事件等请求中的 ID 使用 **DataTable 行名**，不是中文显示名。例如第一本小说的 `BookId` 是 `book_novel`。含有单独 `Id` 字段的表可让它留空，或填与行名相同的值；不能填另一个 ID。

七张表已有 Prototype 路径，日常接入不需要再次执行创建命令。以后检查现有表使用 `-run=ShopBootstrap -VerifyOnly`；不要用重新创建或覆盖示例表的方式保存策划修改。

## 3. 让现有 UI 取得服务

`UShopRunSubsystem` 继承 `UGameInstanceSubsystem`。UE 会随每个 GameInstance 自动创建它，不需要手动 `NewObject`，也不需要更换现有 GameInstance 蓝图或修改其父类。`Get Game Instance` 本身不是 `ShopService` 消息的目标。

建议从现有 UI 根控件开始接入：

1. 打开根 Widget Blueprint，在 **Class Settings → Implemented Interfaces → Add** 选择原生 **Shop View**，然后编译蓝图。
2. 在根控件初始化处调用 **Get Shop Service**，检查返回值有效，保存为 `Shop Service` 变量。变量类型使用返回的 **Shop Run Subsystem Object Reference**。
3. 从该变量调用 **Register View**，`View` 接根控件的 `Self`。返回 `true` 后，逻辑会向它发送 `RefreshShop` 和当前阶段需要的面板消息。
4. 所有 `Request...` 和查询节点的 Target 都接同一个 `Shop Service` 变量。不要让各个子控件自己保存另一份钱、污染或库存作为权威数据。
5. 在“开始游戏”按钮中调用 **Request New Run**，用返回值接 `Branch`；失败分支调用 **Get Last Error**，显示原因或临时用 `Print String` 查看。
6. 根控件销毁或替换时调用 **Unregister View(Self)**。重新创建 UI 只需重新注册并刷新，不应无条件重新开局。

`RequestNewRun` 只允许在 `Boot` 或 `End` 阶段调用。正在经营时重复调用会被拒绝，防止打开新面板时误清空整局。新一局会重新初始化库存、资源、顾客、律令和对话进度。

`RegisterView` 只接受实现了原生 `ShopView` 的对象。原有蓝图接口即使也叫“刷新商店”，也不会自动收到这里的消息；应将现有界面刷新函数接到下面列出的原生接口事件。

## 4. ShopView 的实际事件参数

在根 Widget 的接口列表中实现对应事件。以下十个事件都没有返回值，但输入参数各不相同；不要统一改造成只有一个 Snapshot 的自建事件。

| 原生事件 | 输入参数 | UI 要做的事 |
| --- | --- | --- |
| `RefreshShop` | `Snapshot: FRunSnapshot` | 刷新天数、钱、灵能、污染、启蒙、阶段、当日收支等；需要顾客或单本库存时再查询服务。 |
| `ResolveCustomer` | `CustomerIndex: int32`，`Result: EShopActionResult` | 播放售出、拒绝、无货、选错书或超时的表现，更新队列。 |
| `NewDay` | `Day: int32` | 更新营业日标题与当天界面。 |
| `OpenCalmPanel` | `Candidates: TArray<FName>`，`Snapshot: FRunSnapshot` | 打开镇定界面，用候选 ID 查询律令详情。 |
| `ShowDecreeResult` | `DecreeId: FName`，`Result: FShopCommandResult` | 显示本次律令操作结果。 |
| `ShowObserveResult` | `CustomerIndex: int32`，`Customer: FCustomerRuntime` | 显示当前顾客的观察结果。 |
| `ShowHistoryPanel` | `EventId: FName`，`Event: FEventData` | 显示历史文本与当前支持的见证按钮。 |
| `ShowEnding` | `Ending: EShopEnding`，`Snapshot: FRunSnapshot` | 显示关门、污染释放、归还或循环结局。 |
| `ShowOwlTip` | `LineId: FName`，`Text: FText` | 显示已配置的夜枭台词。 |
| `OpenMarket` | `ItemIds: TArray<FName>`，`Snapshot: FRunSnapshot` | 用商品 ID 查询详情并生成集市条目。 |

回调中只更新界面和读取快照。C++ 在通知期间会拒绝重入的修改命令，避免“刷新事件又立即购买一次”等递归调用；购买、颁布律令等请求应由后续用户点击发起。

也可以绑定子系统的 `OnMoneyChanged`、`OnInventoryChanged`、`OnCustomersChanged`、`OnPollutionChanged` 等事件后主动查询。根 UI 用 `ShopView` 接入通常更直观，无需为同一次操作同时维护两套业务状态。

## 5. UI 可以调用的接口

### 原有 11 个接口

原有名称和返回形式保留，但调用权限由新的完整阶段流程决定。

| 接口 | 参数、返回值 | 使用方式 |
| --- | --- | --- |
| `RequestNewRun` | 无参数 → `bool` | 仅 `Boot/End` 开始新一局。 |
| `GetSnapshot` | 无参数 → `FRunSnapshot` | 获取只读状态副本。 |
| `GetCustomers` | 无参数 → `FCustomerRuntime` 数组 | 当前世界的顾客队列；只操作第一位 `bServed=false` 的顾客。 |
| `GetBookInfo` | `BookId` → `bool`，输出 `BookData`、`Stock` | 获取配置和已经拥有的库存；里书店可收取数量另见 `GetBookRuntime`。 |
| `RequestBeginSell` | `CustomerIndex` → `EShopActionResult` | `Day/Inside` 选择队首；`Opened` 后进入 `Sell/InsideSell`。 |
| `RequestSell` | `BookId` → `EShopActionResult` | 在选书阶段提交书籍。`Sold` 才有收入、扣库存；错误结果要按枚举处理。 |
| `RequestCancelSell` | 无参数 → `bool` | 退出选书回到 `Day/Inside`，不算完成该顾客。 |
| `RequestEndDay` | 无参数 → `bool` | 从 `Day` 到 `DayEnd`；是否允许提前关门由配置控制。 |
| `RequestContinue` | 无参数 → `bool` | 兼容“继续”按钮：日结后进入黄昏选择；夜间活动后结算；夜结后进入集市或次日；集市中则关闭集市。 |
| `RequestRestock` | `BookId` → `bool` | 仅 `Restock` 购买一本表世界书，立即扣成本和记支出。 |
| `RequestNextDay` | 无参数 → `bool` | 仅夜间结算完成且无未处理集市时进入次日；最后一天转结局。 |

选择错误书籍不改钱和库存。默认 `bWrongBookConsumesCustomer=true`，错误选择仍会让顾客离开；没有匹配库存时是否离开由 `bNoMatchConsumesCustomer` 控制。因此 `WrongBook/NoMatch` 虽然不是交易成功，也可能已经推进顾客队列，UI 应以回调和新快照为准。

顾客索引从 `0` 开始。`GetActiveCustomerIndex=-1` 表示当前未打开选书，不表示没有排队顾客。顾客耐心由 C++ 自动推进，只有队首计时；UI 不要再写第二套倒计时扣减逻辑。

### 新增命令

以下接口统一返回 `FShopCommandResult`。

| 接口 | 输入参数 | 有效场景或结果 |
| --- | --- | --- |
| `RequestObserveCustomer` | `CustomerIndex` | 营业或选书时观察队首，标记已观察并回调 `ShowObserveResult`。 |
| `RequestRejectCustomer` | `CustomerIndex` | 营业或选书时拒绝队首，让队列前进。 |
| `RequestOpenInside` | 无 | `DuskChoice` 选择本晚进入里书店。 |
| `RequestOpenRestock` | 无 | `DuskChoice` 选择本晚采购表世界书籍。 |
| `RequestCollectSecret` | `BookId` | `Inside` 消耗灵能，从有限的可收取数量中取得一本秘密书。 |
| `RequestReadSecret` | `BookId` | `Inside` 阅读已拥有的秘密书，结算阅读收益、污染和可配置事件。 |
| `RequestEndNight` | 无 | `Inside/Restock` 完成本晚结算；选书期间需先取消选书。 |
| `RequestPurify` | `Amount: int32` | 轻度污染的 `Calm` 面板，用等量灵能降低污染；数量须为正且不超过现有污染、灵能。 |
| `RequestEnactDecree` | `DecreeId` | `Calm` 颁布今日候选中的可用律令，检查成本、生效和冷却状态。 |
| `RequestSkipDecree` | 无 | 关闭当前 `Calm` 请求并恢复原阶段。 |
| `RequestHistoryChoice` | `Choice: EHistoryChoice` | `History` 处理当前事件；现有枚举只支持 `Witness`。 |
| `RequestOwlTalk` | 无 | 有效游戏中、非 `Calm/History` 时请求台词；缺失台词返回 `Unavailable`。 |
| `RequestOpenMarket` | 无 | 启用集市时，在每周末的 `NightEnd` 打开当周集市。 |
| `RequestBuyMarketItem` | `Id` | `Market` 购买当前商品，价格、禁忌代价、效果、副作用一起结算，失败不扣除部分资源。 |
| `RequestCloseMarket` | 无 | 关闭本晚集市，直接进入次日；最后一天则进入结局。 |

用蓝图 **Break Shop Command Result** 读取：

- `bSucceeded`：本次请求是否成功。
- `Code`：`Success`、`Sold`、`InvalidPhase`、`Unavailable` 等结果枚举，用于分支。
- `Message`：失败原因等显示文本。成功时可能为空。
- `SubjectId`：本次涉及的书籍、律令、事件或商品 ID。

旧接口返回 `false` 或失败枚举时，可立即读取 `GetLastError()` 和 `GetLastResult()`。结果表示最近一次命令，UI 应在当前请求返回后及时保存；不要延迟到之后另一个命令执行完才读取。

其他蓝图查询有 `GetBookIds`、`GetBookRuntime(BookId)`、`GetDecreeInfo(Id)`、`GetMarketItemInfo(Id)`、`GetEventInfo(Id)`、`BuildCustomerNeedText(CustomerIndex)` 和 `HasMatchingStock(Type, Layer)`。其中 `FBookRuntime` 的 `Stock` 是拥有量，`AvailableToCollect` 是本晚剩余可收取量，`ReadCopies` 是已读册数。

默认由项目设置加载配置。需要测试另一组表时，可在 `Boot/End` 调用 `ConfigureTables(Books, Customers, Rules, Decrees, Events, Market, Owl, Seed)`，再调用 `ValidateConfig` 查看输出错误。进行中的一局不能替换配置；编辑了 DataTable 后，重新启动 PIE 会重新加载，单独点击“新一局”使用的是当前已加载的配置副本。

`RandomSeed=-1` 每局使用新种子；填非负固定种子便于复现顾客和经营随机结果。夜枭随机闲聊使用独立的 `CosmeticRandom`，点击闲聊不会改变后续顾客、候选或经营事件的随机结果。

## 6. 阶段应怎样接到按钮

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

一晚只能选择 `Inside` 或 `Restock`，选定后不能在同一晚切换。白天顾客全部处理完后，UI 仍调用 `RequestEndDay`；不要把“顾客数组为空”直接视为整局结束。

`Calm` 和 `History` 会中断当前流程，期间暂停顾客耐心。处理完净化、律令或见证后，C++ 恢复之前阶段；UI 不应自己调用“下一天”来关闭弹窗。多个事件会依次展示，污染达到终止上限或其他终止条件时可能直接进入 `End`。

样例 `MaxDays=35`。第 35 天完成夜间和当晚必要集市后，结算归还或循环，不创建第 36 天。连续负余额或污染极限可提前结束。

## 7. 当前数值与未定稿规则

以下是本次可执行配置和明确标注的解释，不代表策划已经最终定稿。改数值应优先修改 `DT_RunRules.Default` 或相应数据行，再重新加载配置。

| 配置或约定 | 当前执行方式 |
| --- | --- |
| `MaxDays=35`、`Rent=25` | 35 天期限，每日房租 25。均可配置。 |
| `RentTiming=BeforeDusk` | `RequestEndDay` 时扣租；也可配置为 `NightEnd`。同一天不会重复扣租。 |
| `NegativeDaysToClose=3`、`bImmediateBankruptcy=false` | 每次夜结检查余额，连续 3 次夜结为负才关门；恢复非负后计数清零。 |
| 一晚结算 = 一个逻辑回合 | 顾客点击、阅读和弹窗不推进律令回合。`LoopholeDelayTurns=2` 表示颁布后第 2 次夜结触发漏洞。 |
| 律令候选 | 每天抽取可用候选，排除禁用、生效中和冷却中的律令。结构默认候选数 4，Prototype 的 `Default` 行明确改为 3；候选不够时不重复补足。 |
| `PollutedPatienceMultiplier=0.7` | 中度及以上生成的所有顾客，耐心时长乘 0.7；包括里书店顾客。不是每帧额外加速一套倒计时。 |
| 收入倍率 | 当前仅作用于里书店售书收入，按乘积计算后向下取整；表世界售书不乘。它是“当晚收入”暂定解释。灵能收益倍率同样按乘积后向下取整。 |
| `bAllowRepeatRead=false` | 每一册库存只读一次。拥有两册可读两次；售书优先卖出已读册，已读数不会超过库存。 |
| `bRefillSecretOffersEachNight=true` | 每晚选择里书店时，将可收取数量补回该书 `InitialStock`。当晚每收一本减少一次，不能无限领取。 |
| `InitialStock` / `InitialOwnedStock` | 表世界 `InitialStock` 直接成为库存；里世界 `InitialStock` 是待收取数量，`InitialOwnedStock` 才是开局已拥有量。 |
| `StartPsychic=24`、`InsideCustomers=2` | 仅用于验证里书店操作的样例起始灵能和夜间客流，不是最终平衡值。 |
| 秘密书收益覆写 | `PsychicYield=-1` 使用全局阅读灵能收益，`PollutionYield=-1` 使用全局阅读污染。显式填 0 表示零收益或零阅读污染；收书、售书仍使用各自全局污染值。 |
| `RedeemTarget=1500`、`bReturnRequiresRedeemTarget=false` | 赎回目标保留为可配置项，默认不作为归还结局的必需金钱门槛。归还默认要求启蒙至少 60、污染低于 60。 |
| `bStageCrossTriggersLoophole=false` | 跨污染阶段默认不额外触发一次漏洞，避免与定时漏洞重复；是否开启仍待策划确认。 |
| `bEnableMarket=false` | Prototype 暂不开放集市；启用前必须填写商品表并通过校验。 |
| `ClueDropChance=0` | Prototype 不随机掉落线索，需策划填写书籍线索池和概率。 |

污染阈值当前为轻度 31、中度 61、重度 86、终止上限 100，均来自配置。污染一旦达到终止上限会留下标记，即使同一结算中后续效果又降低污染，也不能用它取消已触发的终止条件。

数值字段和文字字段用途不同：`EffectText`、`CostText`、`LoopholeText`、`Text` 负责显示；真正执行的是 `PsychicCost`、`PollutionCut` 以及 `CostEffect`、`Effects`、`LoopholeEffect` 等 `FShopEffect` 数组。只把说明改成“收入减半”不会自动改收入，必须同步填写对应的 `IncomeMultiplier=0.5` 效果。

`FShopEffect.Amount` 对资源效果使用有符号数：例如 `Money=-30` 扣钱，`Pollution=5` 加污染；`Multiplier` 用于倍率。被动效果的 `DurationTurns=0` 跟随所属效果默认时效，`-1` 表示本局永久，正数表示逻辑回合数。当前 `BlockLightSpread` 因原文没有定义可执行的“扩散”规则，会被校验器拒绝；不要为了通过校验随意换成别的效果。

## 8. Prototype 内容的实际范围

`DT_Books` 目前是策划已提供的六本原书：三本表书（潮声集、雨夜诗抄、本镇旧闻）和三本秘密书（潮汐历书、无名女孩的借书卡、革命前夜祷词）。没有补造尚未交付的第 7～16 本书。

`DT_Customers` 有普通、急躁、秘密、污染四种示例模板。当前显示名、权重和需求句属于样例，不能视为正式角色台词。需求句支持 `{类型}`，也兼容 `{BookType}`、`{NeedType}`、`{Type}` 和 `{Name}` 占位符。

`DT_Decrees` 保留七条原始律令记录，其中仅三条启用：

| 行名 / 律令 | 当前状态 |
| --- | --- |
| `bronze_01` 静阅律 | 样例启用；收入 0.8 倍持续 1 次夜结。“下次污染 +5”暂解释为第 2 次夜结漏洞触发时 +5，并非已经实现“下一次污染事件”挂钩。 |
| `bronze_03` 燃烛律 | 样例启用；衰减倍率、灵能收益倍率跟随律令时效。未定量的火污染暂用漏洞 +5 验证框架，此数值不是正式内容。 |
| `silver_02` 守夜律 | 样例启用；衰减倍率随律令失效，每晚 -30 与漏洞后每夜 +3 目前持续本局，重复施法的叠加规则待确认。 |
| `bronze_02` 闭门律 | 禁用；扩散、窗缝与假顾客机制缺少明确规则。 |
| `silver_01` 闭架律 | 禁用；秘密书篡改的实际后果未定稿。 |
| `gold_01` 无名律、`gold_02` 潮汐律 | 禁用；线索删除、永久劣化、重置阈值和潮水反噬等效果仍需完整定义。 |

结构化 `AlterSecretBook` 目前只标记 `FBookRuntime.bAltered`。标记之后究竟改售价、内容、污染还是其他行为，需要策划提供数据和规则；不能据此宣称完整的篡改机制已经完成。

`DT_Events`、`DT_MarketItems`、`DT_Owl` 目前为空。历史事件、集市和夜枭的代码入口可供接入，但没有正式事件、商品或台词内容。夜枭缺内容时返回 `Unavailable`，不会自动生成占位话语；之后需提供 `GuideIndex=1..10` 的十条顺序引导，随机闲聊使用 `GuideIndex=0`。本次没有完成“16 本书”或“110 条夜枭台词”等正式内容量。

`bRequireFinalContentCounts=false` 允许样例用于接入。正式内容准备完后可开启更严格的内容数量校验，但它不能替代策划逐行审稿。P2 存档/读档尚未实现，当前局内状态不会自动持久保存；议价规则尚未定义，本次未编造议价算法或对应按钮。

## 9. 后续验证命令

下面给出运行入口，不代表该文档已经确认构建或测试结果。关闭正在占用该工程的编辑器，在 PowerShell 中执行；实际结果应以当次进程退出码、日志及测试报告为准。

只读检查磁盘上的七张 Prototype 表：

```powershell
& 'D:\epic\UE_5.1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\unreal_project\Libary_release\Libary_Release.uproject' -run=ShopBootstrap -VerifyOnly -unattended -NullRHI -nosplash -log
```

运行程序 A 自动化测试组：

```powershell
& 'D:\epic\UE_5.1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\unreal_project\Libary_release\Libary_Release.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests Bookstore.ProgramA;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\unreal_project\Libary_release\Saved\Automation\ProgramA' -log
```

UE 5.1 中保留上面的 `;Quit`，它是 Automation 队列的退出子命令；仅写 `-TestExit` 可能在报告导出后仍不退出。

UI 接入时先验证最短链路：取得服务 → 注册根视图 → 开局 → 查询顾客 → 选择队首 → 选书售出 → `RefreshShop/ResolveCustomer` 刷新。再逐步接入日结、黄昏互斥选择和夜结，最后接镇定、历史、集市与夜枭。现有界面只有完成这些事件和按钮连线后，才能使用本次 C++ 逻辑。
