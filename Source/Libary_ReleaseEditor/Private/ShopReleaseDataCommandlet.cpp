#include "ShopReleaseDataCommandlet.h"
#include "ShopEndingAuthoring.h"
#include "ShopRunSubsystem.h"
#include "ShopTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopReleaseData, Log, All);

namespace
{
    constexpr const TCHAR* Root = TEXT("/Game/ProgramA/Release/Data/");
    constexpr const TCHAR* ContentNotice = TEXT("Reviewed release baseline, 2026-10-07: 16 books, 4 customer templates, 7 enabled decrees, 5 endings from SourceData/Endings. Initial inventory is owned; progressive-book sales grant +2 Enlighten with 50% probability. The running project uses separate UI rules for the 35-day calendar, history and market features. See Docs/控件蓝图闭环使用说明.md for current behavior.");
    const TCHAR* Names[] = { TEXT("DT_Books"), TEXT("DT_Customers"), TEXT("DT_RunRules"), TEXT("DT_Decrees"), TEXT("DT_Events"), TEXT("DT_MarketItems"), TEXT("DT_Owl"), TEXT("DT_Endings") };

    struct FTables
    {
        TArray<TStrongObjectPtr<UDataTable>> Items;
        UDataTable* At(int32 Index) const { return Items[Index].Get(); }
        template<typename T> UDataTable* Add()
        {
            UDataTable* Table = NewObject<UDataTable>();
            Table->RowStruct = T::StaticStruct();
            Items.Emplace(Table);
            return Table;
        }
    };

    FString PackagePath(int32 Index) { return FString(Root) + Names[Index]; }

    FShopEffect Effect(EShopEffectType Type, int32 Amount = 0, float Multiplier = 1.f, int32 Duration = 0)
    {
        FShopEffect Value;
        Value.Type = Type;
        Value.Amount = Amount;
        Value.Multiplier = Multiplier;
        Value.DurationTurns = Duration;
        return Value;
    }

    bool Validate(const FTables& Tables)
    {
        if (Tables.Items.Num() != UE_ARRAY_COUNT(Names))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Expected eight data tables."));
            return false;
        }
        // GameInstanceSubsystem has Within=GameInstance, so a package is not a valid owner.
        TStrongObjectPtr<UGameInstance> Owner(NewObject<UGameInstance>());
        TStrongObjectPtr<UShopRunSubsystem> Probe(NewObject<UShopRunSubsystem>(Owner.Get()));
        if (!Probe->ConfigureTables(Tables.At(0), Tables.At(1), Tables.At(2), Tables.At(3), Tables.At(4), Tables.At(5), Tables.At(6), 314159, Tables.At(7)))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Configuration rejected: %s"), *Probe->GetLastError().ToString());
            return false;
        }
        FText Error;
        if (!Probe->ValidateConfig(Error))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Validation failed: %s"), *Error.ToString());
            return false;
        }
        for (int32 Index = 0; Index < Tables.Items.Num(); ++Index)
            UE_LOG(LogShopReleaseData, Display, TEXT("Validated %s: %d rows (%s)"), Names[Index], Tables.At(Index)->GetRowMap().Num(), *GetNameSafe(Tables.At(Index)->GetRowStruct()));
        return true;
    }

    bool LoadSaved(FTables& Tables)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
        {
            const FString Path = PackagePath(Index) + TEXT(".") + Names[Index];
            UDataTable* Table = LoadObject<UDataTable>(nullptr, *Path);
            if (!Table)
            {
                UE_LOG(LogShopReleaseData, Error, TEXT("Missing table: %s"), *Path);
                return false;
            }
            Tables.Items.Emplace(Table);
        }
        return true;
    }

    void BuildBooks(UDataTable* Books)
    {
        struct FSourceBook
        {
            const TCHAR* Id;
            EBookType Type;
            EBookLayer Layer;
            int32 Cost, Price, Stock, Psychic, Pollution, Seal;
            const TCHAR* Name;
            bool bProgressive;
        };
        // Source: 数值调参表.xlsx, 书籍表!A4:K19, in original source-row order.
        // User override, 2026-10-06: F is owned inventory; the three marked sales have a 50% +2 reward.
        const FSourceBook Rows[] = {
            { TEXT("book_novel"), EBookType::Novel, EBookLayer::Table, 10, 20, 3, 0, 0, 0, TEXT("《潮声集》"), false },
            { TEXT("book_poem"), EBookType::Poem, EBookLayer::Table, 15, 30, 2, 0, 0, 0, TEXT("《雨夜诗抄》"), false },
            { TEXT("book_history"), EBookType::History, EBookLayer::Table, 20, 40, 2, 0, 0, 0, TEXT("《本镇旧闻》"), false },
            { TEXT("book_novel_02"), EBookType::Novel, EBookLayer::Table, 10, 20, 3, 0, 0, 0, TEXT("《长夜行记》"), false },
            { TEXT("book_novel_03"), EBookType::Novel, EBookLayer::Table, 12, 24, 3, 0, 0, 0, TEXT("《小小的梦》"), true },
            { TEXT("book_poem_02"), EBookType::Poem, EBookLayer::Table, 14, 28, 2, 0, 0, 0, TEXT("《潮汐谣》"), false },
            { TEXT("book_poem_03"), EBookType::Poem, EBookLayer::Table, 16, 32, 2, 0, 0, 0, TEXT("《灯塔十四行》"), false },
            { TEXT("book_history_02"), EBookType::History, EBookLayer::Table, 17, 35, 2, 0, 0, 0, TEXT("《港城编年》"), true },
            { TEXT("book_history_03"), EBookType::History, EBookLayer::Table, 20, 40, 2, 0, 0, 0, TEXT("《疫年纪事》"), true },
            { TEXT("book_secret_01"), EBookType::Secret, EBookLayer::Inside, 0, 60, 1, 8, 5, 1, TEXT("《潮汐历书》"), false },
            { TEXT("book_secret_02"), EBookType::Secret, EBookLayer::Inside, 0, 80, 1, 8, 5, 2, TEXT("《无名女孩的借书卡》"), false },
            { TEXT("book_secret_03"), EBookType::Secret, EBookLayer::Inside, 0, 100, 1, 8, 5, 3, TEXT("《革命前夜祷词》"), false },
            { TEXT("book_secret_04"), EBookType::Secret, EBookLayer::Inside, 0, 70, 1, 8, 5, 1, TEXT("《雾中圣咏》"), false },
            { TEXT("book_secret_05"), EBookType::Secret, EBookLayer::Inside, 0, 90, 1, 10, 6, 2, TEXT("《守望者手记》"), false },
            { TEXT("book_secret_06"), EBookType::Secret, EBookLayer::Inside, 0, 110, 1, 10, 7, 3, TEXT("《无声的弥撒》"), false },
            { TEXT("book_secret_07"), EBookType::Secret, EBookLayer::Inside, 0, 130, 1, 12, 8, 3, TEXT("《最初的锚》"), false }
        };
        for (const FSourceBook& Source : Rows)
        {
            FBookData Row;
            Row.DisplayName = FText::FromString(Source.Name);
            Row.BookType = Source.Type;
            Row.Layer = Source.Layer;
            Row.Cost = Source.Cost;
            Row.Price = Source.Price;
            Row.InitialStock = Source.Stock;
            Row.InitialOwnedStock = 0; // Deprecated compatibility field is never a second grant.
            Row.CollectOfferPerNight = Source.Layer == EBookLayer::Inside ? 1 : 0;
            Row.PsychicYield = Source.Psychic;
            Row.PollutionYield = Source.Pollution;
            Row.SealLevel = Source.Seal;
            Row.SaleEnlightenChance = Source.Layer == EBookLayer::Inside ? 1.f : Source.bProgressive ? 0.5f : 0.f;
            Row.SaleEnlightenYield = Source.Layer == EBookLayer::Inside ? 5 : Source.bProgressive ? 2 : 0;
            Row.EnlightenYield = 0;
            Books->AddRow(FName(Source.Id), Row);
        }
    }

    void BuildCustomers(UDataTable* Customers)
    {
        // Source: 数值调参表.xlsx, 顾客表!A4:H7. Shop-layer eligibility is implemented in ShopCustomers.
        auto Add = [Customers](const TCHAR* Id, ECustomerKind Kind, const TCHAR* Name, const TCHAR* Line, float Patience, float Weight, int32 MinPollution)
        {
            FCustomerData Row;
            Row.Kind = Kind;
            Row.DisplayName = FText::FromString(Name);
            Row.NeedLine = FText::FromString(Line);
            Row.PatienceSeconds = Patience;
            Row.SpawnWeight = Weight;
            Row.MinPollution = MinPollution;
            Row.MaxPollution = 99;
            Customers->AddRow(FName(Id), Row);
        };
        Add(TEXT("customer_normal"), ECustomerKind::Normal, TEXT("普通读者"), TEXT("我想找一本{类型}。"), 30.f, 5.f, 0);
        Add(TEXT("customer_hurry"), ECustomerKind::Hurry, TEXT("赶时间的人"), TEXT("快，给我一本{类型}。"), 15.f, 3.f, 0);
        Add(TEXT("customer_secret"), ECustomerKind::Secret, TEXT("戴兜帽的人"), TEXT("我听说你这里有别的东西。"), 30.f, 2.f, 0);
        Add(TEXT("customer_polluted"), ECustomerKind::Polluted, TEXT("被污染的顾客"), TEXT("你也听见了吗？"), 20.f, 2.f, 61);
    }

    void BuildRules(UDataTable* RulesTable)
    {
        FRunRules Rules;
        // Source: 数值调参表.xlsx, 全局规则!B4:C36. Per-decree costs/cuts live on the decree rows.
        Rules.StartMoney = 100;
        Rules.MaxDays = 35;
        Rules.Rent = 25;
        Rules.RentTiming = ERentTiming::BeforeDusk;
        Rules.NegativeDaysToClose = 3;
        Rules.bImmediateBankruptcy = false;
        Rules.RedeemTarget = 1500;
        Rules.bReturnRequiresRedeemTarget = false;
        Rules.CustomersMin = 3;
        Rules.CustomersMax = 5;
        Rules.WeekTwoCustomerBonus = 1;
        Rules.InsideCustomers = 4; // Legacy field name: night-time FRONT-shop count, not inside-store visitors.
        Rules.DaysPerWeek = 7;
        Rules.StartPsychic = 0;
        Rules.PsychicMax = 0; // Legacy serialized field, no longer used as a cap.
        Rules.StartPollution = 0;
        Rules.PollutionLimit = 100;
        Rules.PollutionDecay = 3;
        Rules.PollutionOnRead = 5;
        Rules.PollutionOnCollect = 5;
        Rules.PollutionOnSell = 10;
        Rules.PollutionOnHistory = 15;
        Rules.ReadPsychicGain = 8;
        Rules.CollectPsychicCost = 12;
        Rules.LoopholeDelayTurns = 2;
        Rules.PatienceNormal = 30.f;
        Rules.PatienceHurry = 15.f;
        Rules.PatienceSecret = 30.f;
        Rules.PatiencePolluted = 20.f;
        Rules.PatienceDropRatePolluted = 0.3f;
        Rules.StartEnlighten = 0;
        Rules.EnlightenWin = 60;
        Rules.WinPollutionThreshold = 60;
        // Stage thresholds: 策划案_V4.4, section 6.2.
        Rules.LightThreshold = 31;
        Rules.MediumThreshold = 61;
        Rules.HeavyThreshold = 86;
        Rules.HeavyGraceTurns = 1;
        Rules.HeavyPenalty = 15;
        Rules.bGoldSatisfiesHeavyGrace = true;
        // Lifecycle rules: 律令漏洞轮换机制说明 + authorized 2026-10-06 implementation decisions.
        Rules.DecreeCandidateCount = 4;
        Rules.DecreeCandidateCountLight = 3;
        Rules.DecreeCandidateCountMedium = 4;
        Rules.DecreeCandidateCountHeavy = 4;
        Rules.DecreeCooldownTurns = 3;
        Rules.bAdvanceTurnOnStageRise = true;
        Rules.bStageCrossTriggersLoophole = false;
        Rules.EmergencyPollutionCut = 5;
        Rules.EmergencyMoneyCost = 20;
        Rules.LightSpreadPerNight = 2;
        Rules.FakeCustomerPollutionPerSecond = 1;
        Rules.FakeCustomerMaxPollution = 5;
        Rules.AlteredBookReadPollution = 2;
        Rules.ReturnedBookReadPollution = 3;
        Rules.ReturnedBookSellPollution = 3;
        Rules.ReturnedBookFallbackPollution = 5;
        Rules.PermanentRentPenalty = 10;
        Rules.PermanentCustomerPenalty = 1;
        Rules.MaxRentPenalty = 30;
        Rules.MaxCustomerPenalty = 2;
        Rules.bEnableHistory = false;
        Rules.bEnableMarket = false;
        Rules.ClueDropChance = 0.f;
        Rules.bRequireCoreContentCounts = true;
        Rules.bRequireFinalContentCounts = false;
        RulesTable->AddRow(TEXT("Default"), Rules);
    }

    void BuildDecrees(UDataTable* Decrees)
    {
        // Source text: 数值调参表.xlsx, 律令表!A4:I10. Typed effects also use 律令漏洞轮换机制说明.
        auto Make = [](const TCHAR* Id, EDecreeQuality Quality, const TCHAR* Name, int32 Psychic, int32 Cut, const TCHAR* Benefit, const TCHAR* Cost, const TCHAR* Loophole, const TCHAR* Text)
        {
            FDecreeData Row;
            Row.Id = FName(Id);
            Row.Quality = Quality;
            Row.DisplayName = FText::FromString(Name);
            Row.PsychicCost = Psychic;
            Row.PollutionCut = Cut;
            Row.EffectText = FText::FromString(Benefit);
            Row.CostText = FText::FromString(Cost);
            Row.LoopholeText = FText::FromString(Loophole);
            Row.Text = FText::FromString(Text);
            Row.LoopholeDelay = 2;
            Row.CooldownTurns = 0;
            Row.MinStage = Quality == EDecreeQuality::Gold ? EPollutionStage::Heavy : (Quality == EDecreeQuality::Silver ? EPollutionStage::Medium : EPollutionStage::Light);
            Row.MaxStage = EPollutionStage::Heavy;
            Row.bEnabled = true;
            return Row;
        };

        FDecreeData Quiet = Make(TEXT("bronze_01"), EDecreeQuality::Bronze, TEXT("静阅律"), 8, 15,
            TEXT("污染 -15"), TEXT("当晚收入 -20%"), TEXT("污染改用低语，下次污染 +5"), TEXT("本店之内，不得高声。"));
        Quiet.CostEffect.Add(Effect(EShopEffectType::NightIncomeMultiplier, 0, 0.8f));
        Quiet.LoopholeEffect.Add(Effect(EShopEffectType::NextPollutionBonus, 5));
        Decrees->AddRow(Quiet.Id, Quiet);

        FDecreeData Door = Make(TEXT("bronze_02"), EDecreeQuality::Bronze, TEXT("闭门律"), 8, 0,
            TEXT("阻止轻度泄露扩散"), TEXT("当晚顾客 -1"), TEXT("污染从窗缝渗入，出现假顾客"), TEXT("日落之后，不得开门。"));
        Door.Effects.Add(Effect(EShopEffectType::BlockLightSpread, 1));
        Door.CostEffect.Add(Effect(EShopEffectType::CustomerCountDelta, -1));
        Door.LoopholeEffect.Add(Effect(EShopEffectType::SpawnFakeCustomer, 1));
        Decrees->AddRow(Door.Id, Door);

        FDecreeData Candle = Make(TEXT("bronze_03"), EDecreeQuality::Bronze, TEXT("燃烛律"), 8, 10,
            TEXT("污染 -10 且衰减翻倍"), TEXT("灵能获取 -20%"), TEXT("火本身成为污染载体"), TEXT("一灯如豆，照不亮角落。"));
        Candle.Effects.Add(Effect(EShopEffectType::DecayMultiplier, 0, 2.f));
        Candle.CostEffect.Add(Effect(EShopEffectType::PsychicGainMultiplier, 0, 0.8f));
        Candle.LoopholeEffect.Add(Effect(EShopEffectType::SkipNightDecay, 1));
        Decrees->AddRow(Candle.Id, Candle);

        FDecreeData Shelf = Make(TEXT("silver_01"), EDecreeQuality::Silver, TEXT("闭架律"), 18, 30,
            TEXT("封锁一次中度泄露(-30)"), TEXT("随机一册秘密书被篡改"), TEXT("被闭架的书自行解锁，污染 +10"), TEXT("日落之后，里架之书不得被翻阅。"));
        Shelf.CostEffect.Add(Effect(EShopEffectType::AlterSecretBook, 1));
        Shelf.LoopholeEffect.Add(Effect(EShopEffectType::UnlockSecretBook, 1));
        Shelf.LoopholeEffect.Add(Effect(EShopEffectType::Pollution, 10));
        Decrees->AddRow(Shelf.Id, Shelf);

        FDecreeData Watch = Make(TEXT("silver_02"), EDecreeQuality::Silver, TEXT("守夜律"), 18, 0,
            TEXT("夜间污染衰减翻倍"), TEXT("每晚金钱 -30"), TEXT("代价照扣，且每次日结污染额外 +3"), TEXT("长夜需有人醒着。"));
        Watch.Effects.Add(Effect(EShopEffectType::DecayMultiplier, 0, 2.f));
        Watch.CostEffect.Add(Effect(EShopEffectType::NightlyMoney, -30, 1.f, -1));
        Watch.LoopholeEffect.Add(Effect(EShopEffectType::NightlyPollution, 3, 1.f, -1));
        Decrees->AddRow(Watch.Id, Watch);

        FDecreeData Nameless = Make(TEXT("gold_01"), EDecreeQuality::Gold, TEXT("无名律"), 35, 50,
            TEXT("清除一次严重泄露(-50)"), TEXT("删除一条历史线索"), TEXT("历史反噬，经营参数永久劣化"), TEXT("凡被历史抹去者，不得在本店留下姓名。"));
        Nameless.CostEffect.Add(Effect(EShopEffectType::RemoveClue, 1));
        Nameless.LoopholeEffect.Add(Effect(EShopEffectType::PermanentBusinessPenalty, 1));
        Decrees->AddRow(Nameless.Id, Nameless);

        FDecreeData Tide = Make(TEXT("gold_02"), EDecreeQuality::Gold, TEXT("潮汐律"), 35, 50,
            TEXT("污染 -50 并重置阈值"), TEXT("随机两册秘密书焚毁"), TEXT("潮水带回被冲走之物"), TEXT("潮水退去时，所有名字都将被带走。"));
        Tide.CostEffect.Add(Effect(EShopEffectType::DestroySecretBooks, 2));
        Tide.Effects.Add(Effect(EShopEffectType::ResetPollutionThresholds));
        Tide.LoopholeEffect.Add(Effect(EShopEffectType::ReturnLostSecretBook, 1));
        Decrees->AddRow(Tide.Id, Tide);
    }

    void BuildEndings(UDataTable* Endings)
    {
        if (!ShopEndingAuthoring::Populate(Endings)) UE_LOG(LogShopReleaseData,Error,TEXT("Cannot load the five documented endings from SourceData/Endings/ending_rows.json"));
    }

    void BuildRelease(FTables& Tables)
    {
        BuildBooks(Tables.Add<FBookData>());
        BuildCustomers(Tables.Add<FCustomerData>());
        BuildRules(Tables.Add<FRunRules>());
        BuildDecrees(Tables.Add<FDecreeData>());
        Tables.Add<FEventData>();
        Tables.Add<FMarketItemData>();
        Tables.Add<FOwlLine>();
        BuildEndings(Tables.Add<FEndingData>());
    }
}

UShopReleaseDataCommandlet::UShopReleaseDataCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UShopReleaseDataCommandlet::Main(const FString& Params)
{
    UE_LOG(LogShopReleaseData, Display, TEXT("%s"), ContentNotice);
    if (FParse::Param(*Params, TEXT("VerifyOnly")))
    {
        FTables Saved;
        if (!LoadSaved(Saved) || !Validate(Saved)) return 1;
        UE_LOG(LogShopReleaseData, Display, TEXT("PROGRAM A RELEASE VERIFY PASSED. Existing assets were only read."));
        return 0;
    }

    // Preflight every destination before the first write. There is deliberately no overwrite switch.
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FString PackageName = PackagePath(Index);
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (FPaths::FileExists(Filename) || FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Refusing to overwrite %s. Use -VerifyOnly to inspect saved tables; edit existing assets in Unreal Editor."), *PackageName);
            return 2;
        }
    }

    FTables Release;
    BuildRelease(Release);
    if (!Validate(Release)) return 3;

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FString PackageName = PackagePath(Index);
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Cannot create destination directory: %s"), *Filename);
            return 4;
        }
        UPackage* Package = CreatePackage(*PackageName);
        UDataTable* Table = DuplicateObject<UDataTable>(Release.At(Index), Package, FName(Names[Index]));
        Table->ClearFlags(RF_Transient);
        Table->SetFlags(RF_Public | RF_Standalone);
        Package->GetMetaData()->SetValue(Table, TEXT("ContentStatus"), ContentNotice);
        Package->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Table, *Filename, Args))
        {
            UE_LOG(LogShopReleaseData, Error, TEXT("Save failed: %s. Earlier tables may exist; existing assets were not overwritten."), *Filename);
            return 5;
        }
        FAssetRegistryModule::AssetCreated(Table);
        UE_LOG(LogShopReleaseData, Display, TEXT("Saved %s (%d rows)"), *PackageName, Table->GetRowMap().Num());
    }
    FTables Saved;
    if (!LoadSaved(Saved) || !Validate(Saved)) return 6;
    UE_LOG(LogShopReleaseData, Display, TEXT("PROGRAM A RELEASE DATA CREATED. Run -VerifyOnly in a fresh process to verify disk reload."));
    return 0;
}
