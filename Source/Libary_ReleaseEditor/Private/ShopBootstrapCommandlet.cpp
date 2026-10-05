#include "ShopBootstrapCommandlet.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogShopBootstrap, Log, All);

namespace
{
    constexpr const TCHAR* Root = TEXT("/Game/ProgramA/Prototype/Data/");
    constexpr const TCHAR* PrototypeNotice = TEXT("PROTOTYPE: six supplied books; four customer templates with sample weights/lines; seven decree records, only three sample interpretations enabled. Events, market and owl lines are intentionally empty. This is not the final authored content.");
    const TCHAR* Names[] = { TEXT("DT_Books"), TEXT("DT_Customers"), TEXT("DT_RunRules"), TEXT("DT_Decrees"), TEXT("DT_Events"), TEXT("DT_MarketItems"), TEXT("DT_Owl") };

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
        // GameInstanceSubsystem has Within=GameInstance. Never create it in a package.
        TStrongObjectPtr<UGameInstance> Owner(NewObject<UGameInstance>());
        TStrongObjectPtr<UShopRunSubsystem> Probe(NewObject<UShopRunSubsystem>(Owner.Get()));
        if (!Probe->ConfigureTables(Tables.At(0), Tables.At(1), Tables.At(2), Tables.At(3), Tables.At(4), Tables.At(5), Tables.At(6), 314159))
        {
            UE_LOG(LogShopBootstrap, Error, TEXT("Configuration rejected: %s"), *Probe->GetLastError().ToString());
            return false;
        }
        FText Error;
        if (!Probe->ValidateConfig(Error))
        {
            UE_LOG(LogShopBootstrap, Error, TEXT("Validation failed: %s"), *Error.ToString());
            return false;
        }
        for (int32 Index = 0; Index < Tables.Items.Num(); ++Index)
            UE_LOG(LogShopBootstrap, Display, TEXT("Validated %s: %d rows (%s)"), Names[Index], Tables.At(Index)->GetRowMap().Num(), *GetNameSafe(Tables.At(Index)->GetRowStruct()));
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
                UE_LOG(LogShopBootstrap, Error, TEXT("Missing table: %s"), *Path);
                return false;
            }
            Tables.Items.Emplace(Table);
        }
        return true;
    }

    void BuildPrototype(FTables& Tables)
    {
        UDataTable* Books = Tables.Add<FBookData>();
        UDataTable* Customers = Tables.Add<FCustomerData>();
        UDataTable* RulesTable = Tables.Add<FRunRules>();
        UDataTable* Decrees = Tables.Add<FDecreeData>();
        Tables.Add<FEventData>();
        Tables.Add<FMarketItemData>();
        Tables.Add<FOwlLine>();

        auto AddBook = [Books](const TCHAR* Id, const TCHAR* Name, EBookType Type, EBookLayer Layer, int32 Cost, int32 Price, int32 Stock)
        {
            FBookData Row;
            Row.DisplayName = FText::FromString(Name);
            Row.BookType = Type;
            Row.Layer = Layer;
            Row.Cost = Cost;
            Row.Price = Price;
            Row.InitialStock = Stock;
            // For Inside rows InitialStock is an offer, not an already owned book.
            Row.InitialOwnedStock = 0;
            Books->AddRow(FName(Id), Row);
        };
        // Names, prices and counts are the six entries supplied in the plan's T19.
        AddBook(TEXT("book_novel"), TEXT("潮声集"), EBookType::Novel, EBookLayer::Table, 10, 20, 3);
        AddBook(TEXT("book_poem"), TEXT("雨夜诗抄"), EBookType::Poem, EBookLayer::Table, 15, 30, 2);
        AddBook(TEXT("book_history"), TEXT("本镇旧闻"), EBookType::History, EBookLayer::Table, 20, 40, 2);
        AddBook(TEXT("book_secret_01"), TEXT("潮汐历书"), EBookType::Secret, EBookLayer::Inside, 0, 60, 1);
        AddBook(TEXT("book_secret_02"), TEXT("无名女孩的借书卡"), EBookType::Secret, EBookLayer::Inside, 0, 80, 1);
        AddBook(TEXT("book_secret_03"), TEXT("革命前夜祷词"), EBookType::Secret, EBookLayer::Inside, 0, 100, 1);

        auto AddCustomer = [Customers](const TCHAR* Id, const TCHAR* Name, ECustomerKind Kind, float Weight, int32 MinPollution)
        {
            FCustomerData Row;
            Row.DisplayName = FText::FromString(Name);
            Row.NeedLine = FText::FromString(TEXT("[Prototype 占位需求] 我想找一本{类型}。"));
            Row.Kind = Kind;
            Row.SpawnWeight = Weight;
            Row.MinPollution = MinPollution;
            Customers->AddRow(FName(Id), Row);
        };
        AddCustomer(TEXT("customer_normal"), TEXT("普通读者"), ECustomerKind::Normal, 1.f, 0);
        AddCustomer(TEXT("customer_hurry"), TEXT("赶时间的人"), ECustomerKind::Hurry, 1.f, 0);
        AddCustomer(TEXT("customer_secret"), TEXT("戴兜帽的人"), ECustomerKind::Secret, 0.2f, 0);
        AddCustomer(TEXT("customer_polluted"), TEXT("被污染的顾客"), ECustomerKind::Polluted, 1.f, 61);

        FRunRules Rules;
        Rules.DecreeCandidateCount = 3;
        Rules.bEnableMarket = false;
        Rules.bRequireFinalContentCounts = false;
        RulesTable->AddRow(TEXT("Default"), Rules);

        auto AddDecree = [](const TCHAR* Id, EDecreeQuality Quality, const TCHAR* Name, int32 Psychic, int32 Cut, const TCHAR* Benefit, const TCHAR* Cost, const TCHAR* Loophole, bool Enabled)
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
            Row.bEnabled = Enabled;
            Row.Text = FText::FromString(Enabled
                ? TEXT("[Prototype] 可执行示例，时效/反噬解释用于验证框架，不是正式策划定稿。")
                : TEXT("[Disabled / 未定稿] 保留策划原文；缺失或含糊的规则尚未转为可执行效果，禁止进入候选池。"));
            return Row;
        };

        FDecreeData Quiet = AddDecree(TEXT("bronze_01"), EDecreeQuality::Bronze, TEXT("静阅律"), 8, 15,
            TEXT("污染 -15"), TEXT("当晚收入 -20%"), TEXT("污染改用低语，下次污染 +5"), true);
        Quiet.CostEffect.Add(Effect(EShopEffectType::IncomeMultiplier, 0, 0.8f, 1));
        Quiet.LoopholeEffect.Add(Effect(EShopEffectType::Pollution, 5));
        Quiet.Text = FText::FromString(TEXT("[Prototype] 收入倍率持续1次夜结；下次污染+5暂解释为第2次夜结漏洞触发时+5，不是定稿的次污染事件挂钩。"));
        Decrees->AddRow(Quiet.Id, Quiet);

        FDecreeData ClosedDoor = AddDecree(TEXT("bronze_02"), EDecreeQuality::Bronze, TEXT("闭门律"), 8, 0,
            TEXT("阻止轻度泄露扩散"), TEXT("当晚顾客 -1"), TEXT("污染从窗缝渗入，出现假顾客"), false);
        Decrees->AddRow(ClosedDoor.Id, ClosedDoor);

        FDecreeData Candle = AddDecree(TEXT("bronze_03"), EDecreeQuality::Bronze, TEXT("燃烛律"), 8, 10,
            TEXT("污染 -10 且衰减翻倍"), TEXT("灵能获取 -20%"), TEXT("火本身成为污染载体"), true);
        Candle.Effects.Add(Effect(EShopEffectType::DecayMultiplier, 0, 2.f));
        Candle.CostEffect.Add(Effect(EShopEffectType::PsychicGainMultiplier, 0, 0.8f));
        Candle.LoopholeEffect.Add(Effect(EShopEffectType::Pollution, 5));
        Candle.Text = FText::FromString(TEXT("[Prototype] 倍率跟随律令生效期；未定量的火污染暂用第2次夜结+5做技术验证。该数值不是正式内容。"));
        Decrees->AddRow(Candle.Id, Candle);

        FDecreeData Shelf = AddDecree(TEXT("silver_01"), EDecreeQuality::Silver, TEXT("闭架律"), 18, 30,
            TEXT("封锁一次中度泄露（-30）"), TEXT("随机一册秘密书被篡改"), TEXT("被闭架的书自行解锁，污染 +10"), false);
        Decrees->AddRow(Shelf.Id, Shelf);

        FDecreeData Watch = AddDecree(TEXT("silver_02"), EDecreeQuality::Silver, TEXT("守夜律"), 18, 0,
            TEXT("夜间污染衰减翻倍"), TEXT("每晚金钱 -30"), TEXT("代价照扣，且每次日结污染额外 +3"), true);
        Watch.Effects.Add(Effect(EShopEffectType::DecayMultiplier, 0, 2.f));
        Watch.CostEffect.Add(Effect(EShopEffectType::NightlyMoney, -30, 1.f, -1));
        Watch.LoopholeEffect.Add(Effect(EShopEffectType::NightlyPollution, 3, 1.f, -1));
        Watch.Text = FText::FromString(TEXT("[Prototype] 衰减倍率随律令失效；每晚-30和漏洞后每夜+3持续本局，重复施法的叠加策略仍需策划确认。"));
        Decrees->AddRow(Watch.Id, Watch);

        FDecreeData Nameless = AddDecree(TEXT("gold_01"), EDecreeQuality::Gold, TEXT("无名律"), 35, 50,
            TEXT("清除一次严重泄露（-50）"), TEXT("删除一条历史线索"), TEXT("历史反噬，经营参数永久劣化"), false);
        Decrees->AddRow(Nameless.Id, Nameless);
        FDecreeData Tide = AddDecree(TEXT("gold_02"), EDecreeQuality::Gold, TEXT("潮汐律"), 35, 50,
            TEXT("污染 -50 并重置阈值"), TEXT("随机两册秘密书焚毁"), TEXT("潮水带回被冲走之物"), false);
        Decrees->AddRow(Tide.Id, Tide);
    }
}

UShopBootstrapCommandlet::UShopBootstrapCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UShopBootstrapCommandlet::Main(const FString& Params)
{
    UE_LOG(LogShopBootstrap, Display, TEXT("%s"), PrototypeNotice);
    if (FParse::Param(*Params, TEXT("VerifyOnly")))
    {
        FTables Saved;
        if (!LoadSaved(Saved) || !Validate(Saved)) return 1;
        UE_LOG(LogShopBootstrap, Display, TEXT("PROTOTYPE VERIFY PASSED. Existing assets were only read."));
        return 0;
    }

    // Refuse all existing destinations before creating or saving the first package.
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FString PackageName = PackagePath(Index);
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (FPaths::FileExists(Filename) || FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
        {
            UE_LOG(LogShopBootstrap, Error, TEXT("Refusing to overwrite %s. Use -VerifyOnly for existing tables."), *PackageName);
            return 2;
        }
    }

    FTables Prototype;
    BuildPrototype(Prototype);
    if (!Validate(Prototype)) return 3;

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        const FString PackageName = PackagePath(Index);
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
        {
            UE_LOG(LogShopBootstrap, Error, TEXT("Cannot create destination directory: %s"), *Filename);
            return 4;
        }
        UPackage* Package = CreatePackage(*PackageName);
        UDataTable* Table = DuplicateObject<UDataTable>(Prototype.At(Index), Package, FName(Names[Index]));
        Table->ClearFlags(RF_Transient);
        Table->SetFlags(RF_Public | RF_Standalone);
        Package->GetMetaData()->SetValue(Table, TEXT("ContentStatus"), PrototypeNotice);
        Package->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Table, *Filename, Args))
        {
            UE_LOG(LogShopBootstrap, Error, TEXT("Save failed: %s. Earlier tables may exist; no existing user assets were overwritten."), *Filename);
            return 5;
        }
        FAssetRegistryModule::AssetCreated(Table);
        UE_LOG(LogShopBootstrap, Display, TEXT("Saved %s (%d rows)"), *PackageName, Table->GetRowMap().Num());
    }
    FTables Saved;
    if (!LoadSaved(Saved) || !Validate(Saved)) return 6;
    UE_LOG(LogShopBootstrap, Display, TEXT("PROTOTYPE BOOTSTRAP PASSED. Run again with -VerifyOnly in a fresh process to verify disk reload."));
    return 0;
}
