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

    bool Validate(const TArray<TStrongObjectPtr<UDataTable>>& Sources, UDataTable* Rules, UDataTable* Decrees)
    {
        FShopCatalog Catalog;
        TMap<FName, FRunRules> RuleRows;
        if (!ReadRows(Sources[0].Get(), Catalog.Books) || !ReadRows(Sources[1].Get(), Catalog.Customers)
            || !ReadRows(Rules, RuleRows) || !ReadRows(Decrees, Catalog.Decrees)
            || !ReadRows(Sources[4].Get(), Catalog.Events) || !ReadRows(Sources[5].Get(), Catalog.MarketItems)
            || !ReadRows(Sources[6].Get(), Catalog.OwlLines) || !ReadRows(Sources[7].Get(), Catalog.Endings)) return false;
        if (RuleRows.Num() != 1)
        {
            UE_LOG(LogShopUIData, Error, TEXT("UI rules must contain exactly one row; existing data was not modified."));
            return false;
        }
        Catalog.Rules = RuleRows.CreateConstIterator().Value();
        if (!Catalog.Rules.bDaytimeOnlyLoop || Catalog.Rules.bEnableHistory || Catalog.Rules.bEnableMarket)
        {
            UE_LOG(LogShopUIData, Error, TEXT("UI rules require daytime-only trading with History and Market disabled; adjust the existing UI table manually."));
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
        UE_LOG(LogShopUIData, Display, TEXT("Explicit UI counter-flow migration: MaxDays %d -> 35; arrival %d [%.2f, %.2f] -> enabled [2, 4]. Other authored values are preserved."),
            Rules->MaxDays, Rules->bUseCustomerArrivalDelay, Rules->CustomerArrivalMin, Rules->CustomerArrivalMax);
        Rules->MaxDays = 35;
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
