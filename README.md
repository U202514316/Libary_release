# Libary_release

## 当前入口与完成状态（2026-10-07）

UE 5.1 打开 `/Game/ProgramA/UI/Maps/L_BookstoreUI`，点击 **Play** 即可运行。当前为 19 个可编辑 UMG 控件蓝图，位于 `/Game/ProgramA/UI`；业务由 C++ `ShopRunSubsystem`统一执行。没有生成或打包 exe。

**开屏已替换为用户提供的 UI（2026-10-07）：**`WBP_MainMenu` 使用《开平.zip》中的完整 1920×1080 原图，并将原有开始／退出按钮的点击区域对齐到图中文字。独立按钮图片用于悬停与按下；原图、人物和标志资源保存在 `SourceArt/UI/MainMenu` 及 `UI/Art/MainMenu`。原有按钮事件、音效及 35 天经营规则保留。306 项界面检查通过，1080p 渲染与原图逐像素一致，720p 显示正常；真实编辑器 PIE 自动化通过“开始 → 十句猫头鹰引导 → 第 1 天”及“退出 → 结束 PIE”。验证调用实际已保存按钮的事件，未做人工鼠标点击或 exe 打包测试。记录见 `Saved/UIBuild/MainMenuArtwork/DeliveryAudit.json`。

流程为主菜单 → 猫头鹰引导 → 白天表店接待 → 白天结算 → 夜间活动 → 夜结 → 次日。当前为 35 天正式期限与五个结局，保留每日不重复顾客立绘、首位及后继顾客 2–4 秒到场、点击顾客后再显示操作、第一层背景及男装主角、点击专属商人后打开采购面板。

**35 天恢复已验证：**仅 `DT_RunRules_UI.Default.MaxDays` 从 8 改为 35，其余业务字段、控件和运行时代码未改。58 项 C++ 测试、31,983 项界面检查通过，实际控件完成 35 天及 105 次售卖，无第 36 天。黑市按第 7、14、21、28、35 夜开放；五结局及重开检查通过。特殊顾客后果与测试范围见 [控件蓝图闭环使用说明](Docs/控件蓝图闭环使用说明.md)，证据在 `Saved/UIBuild/Restore35Days/DeliveryAudit.json`。

**猫头鹰对话后无法开局已修复（2026-10-07）：**此前 C++ 配置校验与已保存的结局表优先级不同，导致最后一句对话调用 `RequestNewRun` 时被拒绝。现已同步结局表，并兼容旧表中“关门／污染”两项的先后顺序；实际判定始终优先处理污染达到 100。已通过实际 WBP 的十句引导、进入第 1 天表书店及顾客到场回归。重新打开项目、在入口地图点击 Play 即可，无需手工修改蓝图。

**启蒙与 HUD 修正：**每获得一张新残页 +10 启蒙；七本里书每成功售出一册固定 +5 启蒙。关闭页面、重复操作失败不会重复奖励。灵能栏已按后续反馈恢复原版完整图标、文字与底栏，只调整数字在原灰色区域内的居中位置；其余状态栏保留。58 项 C++ 测试、20,068 项实际界面检查通过，详见 `Saved/UIBuild/EnlightenHUD/DeliveryAudit.json`。

**灵能与排版更新（2026-10-07）：**灵能取消玩法上限，阅读、夜间补给和律令效果不再截断到 100；状态栏仅显示当前数值。旧 `PsychicMax` 字段只为资产兼容保留，设为 0 且不参与计算。文字按钮改为居中单行，在既有框内按需缩小；状态栏数值也适应固定宽度。现有控件名称、事件图、图像和音频连接保留。

**五结局已按《结局.docx》接入：**任意一天污染达到 100 → 立即被污染吞噬（最高优先级）；连续三次负资金夜结 → 书店关门；期末资金 <1500 → 空书架；期末资金 ≥1500、启蒙 ≥60、污染 <60 时先询问，选择归还 → 真结局，否则 → 赎身离场。期末资金够但真结局条件不足，也进入赎身离场。两个成功结局支付一次 1500，正文完整滚动显示。

当前第 35 夜结算后点击“继续”执行最终判断，不生成第 36 天。`UI/Data/DT_RunRules_UI.Default.MaxDays` 已从 8 恢复为 35，其余规则字段、界面资产和经营代码保持不变。1500 资金与 60 启蒙门槛未调低。快速验证可在 Play 中按 `~` 输入 `ShopTestEnding EmptyShelf / Pollution / Closed / TruthChoice / Redeemed`（五选一，例如 `ShopTestEnding TruthChoice`）；会替换本局，不改数据表。完整说明与接口见 [控件蓝图闭环使用说明](Docs/控件蓝图闭环使用说明.md)。

**此前五结局验证：**Editor 编译、保存资产更新和 54 项 C++ 测试通过。实际控件事件回归 20,018 项检查、0 失败，258 次离屏渲染，覆盖完整 8 天、五种结局、两个选择按钮、编辑器控制台入口、正文滚动、重开及既有玩法；记录见 `Saved/Logs/FiveEndings_UIVerify_Delivery_20261007.log`、`Saved/Automation/FiveEndings_20261007/index.json`。自然 8 天普通经营触发空书架，其他成功分支使用明确标记的临时资源测试，不代表 8 天数值平衡已保证自然可达。原稿和导入来源在 `SourceData/Endings`，本轮备份在 `Saved/Backups/BeforeFiveEndings_20261007`。

**此前灵能与排版验证：**51 项 C++ 经营测试通过；真实 WBP 按钮回归 62,540 项检查、0 失败、224 次离屏渲染，日志 0 错误、0 警告。包含 13 张 1280×720 场景、1920×1080 场景和十位数灵能显示；197 次渲染实际测得按钮／数值文字边界均在容器内。其余为无文字按钮或隐藏状态栏的页面，不计入边界测量。检查了实际渲染图片，未进行人工 PIE 鼠标验收。最终日志：`Saved/Logs/PsychicLayout_Verified_20261007.log`、`Saved/Logs/PsychicLayout_AutomationFinal_20261007.log`；本次备份：`Saved/Backups/BeforeUnlimitedPsychicLayout_20261007`。

**音频已接入（2026-10-07）：**主菜单／引导、表书店、集市、里书店／黑市及结局均已配置音乐；新空书架复用关门配乐，赎身离场复用原循环配乐；按钮、到店、交易、翻阅、残页、污染升级、律令释放与反噬等共 34 个声音用途已连接。`Content/ProgramA/Audio/DA_ShopAudio` 集中调整资源和音量，玩家控制器 `ShopAudio` 组件自动管理播放。新增 26 个 SoundWave、10 个循环 SoundCue，复用 `Content/音效/sfx` 中原有 6 个音效，19 个 WBP 仅补声音节点。场景切换淡入淡出，律令及残页页压低当前音乐且不从头播放。资源对应与蓝图接口见 [控件蓝图闭环使用说明](Docs/控件蓝图闭环使用说明.md)。

最新律令界面已使用新提供的原图，分为两个控件蓝图：`WBP_Decree` 为金／银／铜三排选择界面，点击卡牌只显示红框及右侧立绘；“确定”才释放。“介绍”按钮在确定下方，打开 `WBP_DecreeIntroduction`，左图右文可滚动，返回保留选择。原有阶段、候选、代价与冷却规则继续生效，灰暗卡牌也可查看介绍。相关纹理在 `UI/Art/Decrees`，原图及 SHA256 对应表在 `SourceArt/UI/Decrees`。

此前已完成：

- **里书翻阅**：同一 BookId 每晚一次，基础污染 +10、灵能 +10（不设玩法上限）；一次 30% 判定，成功从未收集的七张残页中抽一张。没有掉落也获得灵能。七份原 DOCX 正文已进入 `DT_Events_UI`及可滚动的 `WBP_HistoryFragment`，收集后不重复；关闭残页不重复结算奖励。翻阅和律令阈值同时发生时，先残页再律令；污染达到 100 则直接进入结局。
- **黑市**：每隔 7 天夜间新增黑市选项，当前为第 7、14、21、28、35 夜，与普通商人／里店互斥。场景和商人复用现有素材，点击商人后购买里书。基础污染 +5；价格暂定基础售价一半，每种每次一册，下一次黑市补货。购买先收入里店库存，之后进入里店上架。数据在 `DT_MarketItems_UI`。
- **律令补完**：表店、售卖书架、里店可手动打开；到店、耐心及伪装顾客污染计时暂停，关闭或颁布后恢复原阶段。显示剩余持续回合、冷却和最近 30 条实际反噬记录。无名律反噬的永久减客现已生效，基础 3 人最低 1 人；闭架律篡改书的额外阅读污染也有了实际入口。

翻阅／购买数值为基础增量，已有律令的下次污染加成、篡改书和污染返架书可以叠加。四条 UI 专用资金代价继续沿用，原 Release 律令表不改；七张残页与旧线索 Clues 分开，不会因无名律删除。具体七条效果、代价与漏洞见 [控件蓝图闭环使用说明](Docs/控件蓝图闭环使用说明.md)。

夜结仍恢复 8 灵能，并在秘密书总数不足 7 时免费补 1 册；此上限不限制黑市购买。所有顾客只在白天表店出现，里店没有经营。原猫头鹰、普通商人、背景、封面及人物立绘保留。主菜单与引导结束前不调用 `RequestNewRun`；不要在自己的蓝图中再次创建或注册 Root。

**此前音频轮次验证：**Editor 及 19 个 WBP 编译成功；真实 WBP 按钮回归 **58,622 项检查、0 失败，197 次离屏渲染，0 错误、0 警告**。保持 35 天、四结局、残页、黑市及律令界面的原有检查，同时验证配乐切换、弹窗压低音乐、购买与售卖结果音效及到店去重。另通过 304 项音频边界检查。UE 离线混音器已实际输出全部 34 个声音用途的波形，包含主菜单音乐第二轮循环，单路默认音量未削波；不是人工扬声器试听或 PIE 鼠标验收。该轮此前 50 项 C++ 经营测试通过，仅修改声音展示层。

音频轮次记录：`Saved/Logs/Audio_UIVerify_20261007.log`、`Saved/Logs/Audio_Edges_Verified_20261007.log`、`Saved/Logs/Audio_Playback_Verified_20261007.log`、`Saved/Audio/EngineRender/Report.tsv`；该轮备份为 `Saved/Backups/BeforeAudio_20261007`。原律令界面验收保存在 `Saved/UIBuild/DecreeArtworkDeliveryAudit.json`。直接打开 UE 运行即可；无需再次生成资产，不要在手工编辑 WBP 后运行 `ShopUIBuild -Rebuild`。

本局残页及翻阅记录当前在内存中，新开局重置，退出不会自动保存。完整剧情、完整猫头鹰随机台词、存档读档和议价仍为后续工作。原先书名差异继续保留：《小小的梦》暂用《书记员的梦》封面。

旧规则兼容：`bDaytimeOnlyLoop=false`时仍保留早期夜间表店营业；新残页、黑市、减客分别由 `bUseHistoryFragments/bSecretBookMarket/bApplyDaytimeCustomerPenalty`开启。Release 数据和旧测试保留。

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
