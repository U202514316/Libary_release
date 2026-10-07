#include "ShopUIData.h"
#include "ShopUIAuthoring.h"
#include "ShopTypes.h"
#include "ShopValidation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUIData, Log, All);

namespace
{
    constexpr const TCHAR* ReleaseRoot = TEXT("/Game/ProgramA/Release/Data/");
    constexpr const TCHAR* UIRoot = TEXT("/Game/ProgramA/UI/Data/");
    const TCHAR* SourceNames[] = { TEXT("DT_Books"), TEXT("DT_Customers"), TEXT("DT_RunRules"), TEXT("DT_Decrees"),
        TEXT("DT_Events"), TEXT("DT_MarketItems"), TEXT("DT_Owl"), TEXT("DT_Endings") };
    const TCHAR* UINames[] = { TEXT("DT_RunRules_UI"), TEXT("DT_Decrees_UI") };

    UDataTable* LoadTable(const FString& PackageName)
    {
        const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
        UDataTable* Table = LoadObject<UDataTable>(nullptr, *ObjectPath);
        if (!Table) UE_LOG(LogShopUIData, Error, TEXT("Cannot load DataTable %s; existing packages will not be replaced."), *ObjectPath);
        return Table;
    }

    template<typename T> bool ReadRows(const UDataTable* Table, TMap<FName, T>& Out)
    {
        if (!Table || Table->GetRowStruct() != T::StaticStruct())
        {
            UE_LOG(LogShopUIData, Error, TEXT("Unexpected row structure in %s; expected %s."), *GetPathNameSafe(Table), *T::StaticStruct()->GetName());
            return false;
        }
        Out.Reset();
        for (const auto& Pair : Table->GetRowMap())
        {
            const T* Row = Table->FindRow<T>(Pair.Key, TEXT("ShopUIData"), false);
            if (!Row) return false;
            Out.Add(Pair.Key, *Row);
        }
        return true;
    }

    bool CustomizeRules(UDataTable* Table)
    {
        if (!Table || Table->GetRowStruct() != FRunRules::StaticStruct() || Table->GetRowMap().Num() != 1)
        {
            UE_LOG(LogShopUIData, Error, TEXT("Release run rules must have exactly one FRunRules row."));
            return false;
        }
        const FName RowName = Table->GetRowNames()[0];
        FRunRules* Rules = Table->FindRow<FRunRules>(RowName, TEXT("ShopUIData"), false);
        Rules->bDaytimeOnlyLoop = true;
        Rules->bUniqueDailyCustomerPortraits = true;
        Rules->CustomersMin = Rules->CustomersMax = 3;
        Rules->WeekTwoCustomerBonus = 0;
        Rules->InsideCustomers = 0;
        Rules->StartPsychic = 8;
        Rules->MaxDays = 35; // Full calendar; short verification fixtures remain transient.
        Rules->bReturnRequiresRedeemTarget = true;
        Rules->PsychicMax = 0; // Legacy field; rewards are no longer capped.
        Rules->NightlyPsychicGain = 8;
        Rules->NightlySecretSupply = 1;
        Rules->SecretOwnedCap = 7;
        Rules->bAllowEarlyClose = false;
        Rules->bEnableMarket = false;
        Rules->bEnableHistory = false;
        // MaxDays, prices, pollution thresholds and every unlisted rule remain authored Release values.
        return true;
    }

    bool ReplaceCost(UDataTable* Table, const TCHAR* Id, EShopEffectType OldType, int32 MoneyCost)
    {
        FDecreeData* Row = Table->FindRow<FDecreeData>(FName(Id), TEXT("ShopUIData"), false);
        if (!Row)
        {
            UE_LOG(LogShopUIData, Error, TEXT("Missing Release decree %s; refusing to invent its effects."), Id);
            return false;
        }
        int32 Matches = 0;
        for (const FShopEffect& Effect : Row->CostEffect) if (Effect.Type == OldType) ++Matches;
        if (Matches != 1)
        {
            UE_LOG(LogShopUIData, Error, TEXT("Release decree %s must contain exactly one expected cost effect (%d); found %d."), Id, static_cast<int32>(OldType), Matches);
            return false;
        }
        for (FShopEffect& Effect : Row->CostEffect)
        {
            if (Effect.Type != OldType) continue;
            Effect = FShopEffect();
            Effect.Type = EShopEffectType::Money;
            Effect.Amount = -MoneyCost;
        }
        Row->CostText = FText::FromString(FString::Printf(TEXT("立即资金 -%d。"), MoneyCost));
        // Text is the decree's narrative line, not the numeric cost; preserve its authored wording.
        return true;
    }

    bool CustomizeDecrees(UDataTable* Table)
    {
        if (!Table || Table->GetRowStruct() != FDecreeData::StaticStruct())
        {
            UE_LOG(LogShopUIData, Error, TEXT("Release decrees must use FDecreeData rows."));
            return false;
        }
        return ReplaceCost(Table, TEXT("bronze_01"), EShopEffectType::NightIncomeMultiplier, 10)
            && ReplaceCost(Table, TEXT("bronze_02"), EShopEffectType::CustomerCountDelta, 8)
            && ReplaceCost(Table, TEXT("bronze_03"), EShopEffectType::PsychicGainMultiplier, 10)
            && ReplaceCost(Table, TEXT("gold_01"), EShopEffectType::RemoveClue, 40);
    }

    bool Validate(const TArray<TStrongObjectPtr<UDataTable>>& Sources, UDataTable* Rules, UDataTable* Decrees,
        UDataTable* Events = nullptr, UDataTable* Market = nullptr)
    {
        FShopCatalog Catalog;
        TMap<FName, FRunRules> RuleRows;
        if (!ReadRows(Rules, RuleRows) || RuleRows.Num() != 1) return false;
        const FRunRules& SelectedRules = RuleRows.CreateConstIterator().Value();
        if (!Events) Events = SelectedRules.bUseHistoryFragments ? LoadTable(FString(UIRoot) + TEXT("DT_Events_UI")) : Sources[4].Get();
        if (!Market) Market = SelectedRules.bSecretBookMarket ? LoadTable(FString(UIRoot) + TEXT("DT_MarketItems_UI")) : Sources[5].Get();
        if (!ReadRows(Sources[0].Get(), Catalog.Books) || !ReadRows(Sources[1].Get(), Catalog.Customers)
            || !ReadRows(Rules, RuleRows) || !ReadRows(Decrees, Catalog.Decrees)
            || !ReadRows(Events, Catalog.Events) || !ReadRows(Market, Catalog.MarketItems)
            || !ReadRows(Sources[6].Get(), Catalog.OwlLines) || !ReadRows(Sources[7].Get(), Catalog.Endings)) return false;
        if (RuleRows.Num() != 1)
        {
            UE_LOG(LogShopUIData, Error, TEXT("UI rules must contain exactly one row; existing data was not modified."));
            return false;
        }
        Catalog.Rules = RuleRows.CreateConstIterator().Value();
        if (!Catalog.Rules.bDaytimeOnlyLoop)
        {
            UE_LOG(LogShopUIData, Error, TEXT("UI rules require daytime-only trading."));
            return false;
        }
        // User-adjusted numbers are valid as long as the catalog's runtime contract remains valid.
        FText Error;
        if (!ShopValidation::Validate(Catalog, Error))
        {
            UE_LOG(LogShopUIData, Error, TEXT("UI data validation failed: %s. Existing assets were not modified."), *Error.ToString());
            return false;
        }
        return true;
    }
}

bool ShopUIData::UpgradeHistoryMarketDecrees()
{
    TArray<TStrongObjectPtr<UDataTable>> Sources;
    for (const TCHAR* Name : SourceNames)
    {
        UDataTable* Source = LoadTable(FString(ReleaseRoot) + Name);
        if (!Source) return false;
        Sources.Emplace(Source);
    }
    UDataTable* Rules = LoadTable(FString(UIRoot) + UINames[0]);
    UDataTable* Decrees = LoadTable(FString(UIRoot) + UINames[1]);
    if (!Rules || !Decrees || Rules->GetRowStruct() != FRunRules::StaticStruct() || Rules->GetRowNames().Num() != 1) return false;
    TStrongObjectPtr<UDataTable> Candidate(DuplicateObject<UDataTable>(Rules, GetTransientPackage()));
    const FName RuleId = Candidate->GetRowNames()[0];
    FRunRules* Row = Candidate->FindRow<FRunRules>(RuleId, TEXT("HistoryMarketMigration"));
    Row->bEnableHistory = Row->bUseHistoryFragments = true;
    Row->HistoryFragmentChance = .3f; Row->ReadPsychicGain = 10; Row->PollutionOnRead = 10; Row->ClueDropChance = 0.f;
    Row->HistoryFragmentEnlightenGain = 10;
    Row->bEnableMarket = Row->bSecretBookMarket = true; Row->MarketBookPollution = 5; Row->DaysPerWeek = 7;
    Row->bApplyDaytimeCustomerPenalty = true;
    TStrongObjectPtr<UDataTable> Events(NewObject<UDataTable>()); Events->RowStruct = FEventData::StaticStruct();
    TStrongObjectPtr<UDataTable> Market(NewObject<UDataTable>()); Market->RowStruct = FMarketItemData::StaticStruct();
    FString Json; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir()/TEXT("SourceArt/History/history_fragments.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Fragments = nullptr;
    if (!Root->TryGetArrayField(TEXT("fragments"), Fragments) || Fragments->Num() != 7) return false;
    for (const TSharedPtr<FJsonValue>& Value : *Fragments)
    {
        const TSharedPtr<FJsonObject> Page = Value->AsObject(); FString Id, Title, Body;
        if (!Page || !Page->TryGetStringField(TEXT("id"), Id) || !Page->TryGetStringField(TEXT("title"), Title) || !Page->TryGetStringField(TEXT("text"), Body)) return false;
        FEventData Event; Event.Id = FName(*Id); Event.Title = FText::FromString(Title); Event.Text = FText::FromString(Body);
        Event.OptionA = FText::FromString(TEXT("收好残页 · 返回")); Events->AddRow(Event.Id, Event);
    }
    for (const auto& Pair : Sources[0]->GetRowMap())
    {
        const FBookData* Book = Sources[0]->FindRow<FBookData>(Pair.Key, TEXT("MarketMigration"));
        if (!Book || Book->Layer != EBookLayer::Inside || Book->BookType != EBookType::Secret) continue;
        FMarketItemData Item; Item.Id = Pair.Key; Item.SecretBookId = Pair.Key; Item.DisplayName = Book->DisplayName;
        Item.Category = TEXT("SecretBook"); Item.Price = FMath::Max(1, FMath::RoundToInt(Book->Price * .5f)); Item.bOnePerRun = false;
        Market->AddRow(Item.Id, Item);
    }
    // Preserve already-authored extension tables on reruns; import never silently replaces edits.
    const TCHAR* Names[] = {TEXT("DT_Events_UI"), TEXT("DT_MarketItems_UI")};
    UDataTable* Tables[] = {Events.Get(), Market.Get()}; bool Existing[] = {false, false};
    for (int32 Index=0; Index<2; ++Index)
    {
        const FString Path = FString(UIRoot) + Names[Index];
        if (FPackageName::DoesPackageExist(Path)) { Existing[Index] = true; Tables[Index] = LoadTable(Path); if (!Tables[Index]) return false; }
    }
    if (!Validate(Sources, Candidate.Get(), Decrees, Tables[0], Tables[1])) return false;
    for (int32 Index=0; Index<2; ++Index)
    {
        if (Existing[Index]) continue;
        UPackage* Package = CreatePackage(*(FString(UIRoot) + Names[Index]));
        UDataTable* Saved = DuplicateObject<UDataTable>(Tables[Index], Package, Names[Index]);
        Saved->ClearFlags(RF_Transient); Saved->SetFlags(RF_Public | RF_Standalone);
        if (!ShopUIAuthoring::Save(Saved)) return false;
        FAssetRegistryModule::AssetCreated(Saved);
    }
    Rules->AddRow(RuleId, *Row);
    if (!ShopUIAuthoring::Save(Rules)) return false;
    UE_LOG(LogShopUIData, Display, TEXT("HISTORY/MARKET/DECREE DATA READY: 7 exact document texts, 7 secret-book offers; only explicit feature rules changed."));
    return true;
}

bool ShopUIData::Restore35Days()
{
    TArray<TStrongObjectPtr<UDataTable>> Sources;
    for (const TCHAR* Name : SourceNames)
    {
        UDataTable* Source=LoadTable(FString(ReleaseRoot)+Name);
        if (!Source) return false;
        Sources.Emplace(Source);
    }
    UDataTable* Rules=LoadTable(FString(UIRoot)+UINames[0]);
    UDataTable* Decrees=LoadTable(FString(UIRoot)+UINames[1]);
    if (!Rules || !Decrees || Rules->GetRowStruct()!=FRunRules::StaticStruct() || Rules->GetRowNames().Num()!=1) return false;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("UIBuild/Restore35Days");
    IFileManager::Get().MakeDirectory(*Directory,true);
    const auto Export=[&Directory](UDataTable* Table,const FString& Name)
    {
        return FFileHelper::SaveStringToFile(Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs),
            *(Directory/(Name+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    if (!Export(Rules,TEXT("DT_RunRules_UI_Before"))) return false;
    TStrongObjectPtr<UDataTable> Candidate(DuplicateObject<UDataTable>(Rules,GetTransientPackage()));
    const FName RuleId=Candidate->GetRowNames()[0];
    FRunRules* Row=Candidate->FindRow<FRunRules>(RuleId,TEXT("Restore35Days"));
    const int32 Before=Row->MaxDays;
    Row->MaxDays=35;
    if (!Validate(Sources,Candidate.Get(),Decrees)) return false;
    Rules->AddRow(RuleId,*Row);
    if (!ShopUIAuthoring::Save(Rules) || !Export(Rules,TEXT("DT_RunRules_UI"))) return false;
    for (UDataTable* Table : {Sources[0].Get(),Sources[1].Get(),Decrees})
        if (!Export(Table,Table->GetName())) return false;
    UE_LOG(LogShopUIData,Display,TEXT("Restored UI MaxDays %d -> 35; all other rule fields, business data and widgets preserved."),Before);
    return true;
}

bool ShopUIData::Build(bool bUpgradeCounterFlow, bool bUpgradeUniquePortraits)
{
    TArray<TStrongObjectPtr<UDataTable>> Sources;
    for (const TCHAR* Name : SourceNames)
    {
        UDataTable* Source = LoadTable(FString(ReleaseRoot) + Name);
        if (!Source) return false;
        Sources.Emplace(Source);
    }

    TArray<TStrongObjectPtr<UDataTable>> Candidates;
    bool NewTables[2] = { false, false };
    TStrongObjectPtr<UDataTable> ExistingRules(nullptr);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(UINames); ++Index)
    {
        const FString PackageName = FString(UIRoot) + UINames[Index];
        const bool bExists = FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName) != nullptr;
        if (bExists)
        {
            UDataTable* Existing = LoadTable(PackageName);
            if (!Existing) return false;
            if (Index == 0 && (bUpgradeCounterFlow || bUpgradeUniquePortraits))
            {
                ExistingRules.Reset(Existing);
                UDataTable* Candidate = DuplicateObject<UDataTable>(Existing, GetTransientPackage(),
                    MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("CounterFlowRules")));
                if (!Candidate) return false;
                Candidate->ClearFlags(RF_Public | RF_Standalone);
                Candidate->SetFlags(RF_Transient);
                Candidates.Emplace(Candidate);
            }
            else Candidates.Emplace(Existing);
            UE_LOG(LogShopUIData, Display, TEXT("Preserving existing UI data and user values: %s"), *PackageName);
        }
        else
        {
            UDataTable* Candidate = DuplicateObject<UDataTable>(Sources[Index + 2].Get(), GetTransientPackage(),
                MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), FName(UINames[Index])));
            if (!Candidate) return false;
            Candidate->ClearFlags(RF_Public | RF_Standalone);
            Candidate->SetFlags(RF_Transient);
            Candidates.Emplace(Candidate);
            NewTables[Index] = true;
            if (Index == 0 ? !CustomizeRules(Candidate) : !CustomizeDecrees(Candidate)) return false;
        }
    }
    if (bUpgradeCounterFlow)
    {
        UDataTable* Table = Candidates[0].Get();
        if (Table->GetRowStruct() != FRunRules::StaticStruct() || Table->GetRowNames().Num() != 1) return false;
        FRunRules* Rules = Table->FindRow<FRunRules>(Table->GetRowNames()[0], TEXT("CounterFlowMigration"));
        UE_LOG(LogShopUIData, Display, TEXT("Explicit UI counter-flow migration: preserve MaxDays %d; arrival %d [%.2f, %.2f] -> enabled [2, 4]. Other authored values are preserved."),
            Rules->MaxDays, Rules->bUseCustomerArrivalDelay, Rules->CustomerArrivalMin, Rules->CustomerArrivalMax);
        Rules->bUseCustomerArrivalDelay = true;
        Rules->CustomerArrivalMin = 2.f;
        Rules->CustomerArrivalMax = 4.f;
    }
    // Validate both candidates together before the first package write.
    if (bUpgradeUniquePortraits)
    {
        UDataTable* Table = Candidates[0].Get();
        if (Table->GetRowStruct() != FRunRules::StaticStruct() || Table->GetRowNames().Num() != 1) return false;
        FRunRules* Rules = Table->FindRow<FRunRules>(Table->GetRowNames()[0], TEXT("UniquePortraitMigration"));
        UE_LOG(LogShopUIData, Display, TEXT("Explicit UI portrait migration: bUniqueDailyCustomerPortraits %d -> true; all other values preserved."),
            Rules->bUniqueDailyCustomerPortraits);
        Rules->bUniqueDailyCustomerPortraits = true;
    }
    if (!Validate(Sources, Candidates[0].Get(), Candidates[1].Get())) return false;
    if (ExistingRules.IsValid())
    {
        const FName RowName = Candidates[0]->GetRowNames()[0];
        const FRunRules* Upgraded = Candidates[0]->FindRow<FRunRules>(RowName, TEXT("CounterFlowMigration"));
        ExistingRules->AddRow(RowName, *Upgraded);
        if (!ShopUIAuthoring::Save(ExistingRules.Get()))
        {
            UE_LOG(LogShopUIData, Error, TEXT("Could not save the explicitly requested UI rules migration."));
            return false;
        }
        UE_LOG(LogShopUIData, Display, TEXT("Migrated only /Game/ProgramA/UI/Data/DT_RunRules_UI; Release data and UI decrees preserved."));
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(UINames); ++Index)
    {
        if (!NewTables[Index]) continue;
        const FString PackageName = FString(UIRoot) + UINames[Index];
        if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
        {
            UE_LOG(LogShopUIData, Error, TEXT("Destination appeared during validation; refusing to overwrite %s."), *PackageName);
            return false;
        }
        UPackage* Package = CreatePackage(*PackageName);
        UDataTable* Table = DuplicateObject<UDataTable>(Candidates[Index].Get(), Package, FName(UINames[Index]));
        if (!Table) return false;
        Table->ClearFlags(RF_Transient);
        Table->SetFlags(RF_Public | RF_Standalone);
        Package->GetMetaData()->SetValue(Table, TEXT("ContentStatus"),
            TEXT("UI-only daytime loop copied from Release. Existing values are preserved except explicit -UpgradeCounterFlow or -UpgradeUniquePortraits migrations."));
        if (!ShopUIAuthoring::Save(Table))
        {
            UE_LOG(LogShopUIData, Error, TEXT("Failed to save %s. Earlier newly created tables may exist; rerunning preserves them."), *PackageName);
            return false;
        }
        FAssetRegistryModule::AssetCreated(Table);
        UE_LOG(LogShopUIData, Display, TEXT("Created %s (%d rows); Release source remains unchanged."), *PackageName, Table->GetRowMap().Num());
    }
    UE_LOG(LogShopUIData, Display, TEXT("UI DATA READY: daytime rules and adapted decree costs validated; counter-flow migration=%d."), bUpgradeCounterFlow);
    return true;
}
