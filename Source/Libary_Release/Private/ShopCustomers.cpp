#include "ShopCustomers.h"
#include "ShopEconomy.h"
#include "Misc/Crc.h"

#include <cmath>

#define LOCTEXT_NAMESPACE "ShopCustomers"

namespace
{
    bool CustomerFail(FText& Error, const FText& Message)
    {
        Error = Message;
        return false;
    }

    bool ValidTableNeed(EBookType Type)
    {
        return Type == EBookType::Novel || Type == EBookType::Poem || Type == EBookType::History;
    }

    float RulePatience(const FRunRules& Rules, ECustomerKind Kind)
    {
        switch (Kind)
        {
        case ECustomerKind::Normal: return Rules.PatienceNormal;
        case ECustomerKind::Hurry: return Rules.PatienceHurry;
        case ECustomerKind::Secret: return Rules.PatienceSecret;
        case ECustomerKind::Polluted: return Rules.PatiencePolluted;
        default: return -1.f;
        }
    }

    FText TypeName(EBookType Type)
    {
        switch (Type)
        {
        case EBookType::Novel: return LOCTEXT("Novel", "小说");
        case EBookType::Poem: return LOCTEXT("Poem", "诗歌");
        case EBookType::History: return LOCTEXT("History", "历史");
        case EBookType::Secret: return LOCTEXT("Secret", "密文书");
        default: return LOCTEXT("UnknownType", "未知类型");
        }
    }
}

int32 ShopCustomers::PortraitCapacity(ECustomerKind Kind)
{
    switch (Kind)
    {
    case ECustomerKind::Normal: return 4;
    case ECustomerKind::Hurry: return 1;
    case ECustomerKind::Secret: return 1;
    case ECustomerKind::Polluted: return 2;
    default: return 0;
    }
}

int32 ShopCustomers::ChoosePortrait(ECustomerKind Kind, int32 Day, int32 QueueIndex, FName TemplateId,
    const TArray<FCustomerRuntime>& ReservedCustomers, int32 ExcludeIndex)
{
    const int32 Capacity = PortraitCapacity(Kind);
    if (Capacity == 0) return INDEX_NONE;
    const int32 First = Kind == ECustomerKind::Normal ? 0 : Kind == ECustomerKind::Hurry ? 4 : Kind == ECustomerKind::Secret ? 5 : 6;
    const FString Identity = FString::Printf(TEXT("%d|%d|%s"), Day, QueueIndex, *TemplateId.ToString());
    const int32 Start = static_cast<int32>(FCrc::StrCrc32(*Identity) % static_cast<uint32>(Capacity));
    for (int32 Offset = 0; Offset < Capacity; ++Offset)
    {
        const int32 Slot = First + (Start + Offset) % Capacity;
        bool bReserved = false;
        for (int32 Index = 0; Index < ReservedCustomers.Num(); ++Index)
            if (Index != ExcludeIndex && ReservedCustomers[Index].PortraitSlot == Slot) { bReserved = true; break; }
        // Served and waiting customers both reserve their faces until the next day.
        if (!bReserved) return Slot;
    }
    return INDEX_NONE;
}

bool ShopCustomers::Generate(FShopRunState& State, const FShopCatalog& Catalog, EBookLayer Layer, FText& Error)
{
    Error = FText::GetEmpty();
    if (Layer != EBookLayer::Table && Layer != EBookLayer::Inside)
        return CustomerFail(Error, LOCTEXT("InvalidTimeMapping", "顾客昼夜兼容参数无效。"));
    return GenerateForTime(State, Catalog, Layer == EBookLayer::Inside, Error);
}

bool ShopCustomers::GenerateForTime(FShopRunState& State, const FShopCatalog& Catalog, bool bNight, FText& Error)
{
    Error = FText::GetEmpty();
    const FRunRules& Rules = Catalog.Rules;
    if (Rules.bUniqueDailyCustomerPortraits && !Rules.bDaytimeOnlyLoop)
        return CustomerFail(Error, LOCTEXT("PortraitLoop", "每日立绘不重复规则需要启用三位顾客的白天经营流程。"));
    if (Rules.bDaytimeOnlyLoop && bNight)
        return CustomerFail(Error, LOCTEXT("DaytimeOnly", "当前规则只在白天营业，夜间不生成顾客。"));
    if (Rules.CustomersMin < 0 || Rules.CustomersMax < Rules.CustomersMin || Rules.CustomersMax > 10000 || Rules.InsideCustomers < 0 || Rules.InsideCustomers > 10000 || Rules.DaysPerWeek <= 0)
        return CustomerFail(Error, LOCTEXT("InvalidCounts", "每日顾客数量或每周天数配置无效。"));
    if (!std::isfinite(Rules.CustomerArrivalMin) || !std::isfinite(Rules.CustomerArrivalMax) ||
        Rules.CustomerArrivalMin < 0.f || Rules.CustomerArrivalMax < Rules.CustomerArrivalMin)
        return CustomerFail(Error, LOCTEXT("InvalidArrival", "顾客到场间隔必须为有限非负数，且最小值不大于最大值。"));

    // Generate with a copied stream; configuration failures must not consume randomness.
    FRandomStream Random = State.Random;
    // InsideCustomers is the serialized legacy name for the nighttime surface-shop count.
    int64 Count = Rules.bDaytimeOnlyLoop ? 3 : (bNight ? Rules.InsideCustomers : Random.RandRange(Rules.CustomersMin, Rules.CustomersMax));
    if (!Rules.bDaytimeOnlyLoop && !bNight && State.Day > Rules.DaysPerWeek) Count += Rules.WeekTwoCustomerBonus;
    if (State.CustomerPenalty < 0) return CustomerFail(Error, LOCTEXT("InvalidCustomerPenalty", "永久顾客惩罚配置无效。"));
    if (!Rules.bDaytimeOnlyLoop) Count -= State.CustomerPenalty;
    if (bNight)
        for (const FShopModifier& Modifier : State.Modifiers)
            if (Modifier.Type == EShopEffectType::CustomerCountDelta && (Modifier.EndTurn < 0 || State.Turn < Modifier.EndTurn)) Count += Modifier.Amount;
    Count = FMath::Max<int64>(0, Count);
    if (Count > 10000) return CustomerFail(Error, LOCTEXT("TooManyCustomers", "单批顾客数量超过支持的 10000 人。"));
    if (Count == 0)
    {
        State.Customers.Reset();
        State.ActiveCustomer = INDEX_NONE;
        State.Random = Random;
        ResetArrival(State, Rules);
        return true;
    }

    TArray<EBookType> Needs;
    for (EBookType Type : Rules.RandomNeedPool)
    {
        if (!ValidTableNeed(Type)) return CustomerFail(Error, LOCTEXT("InvalidNeed", "普通书需求池只能包含小说、诗歌和历史。"));
        Needs.Add(Type);
    }
    if (Needs.IsEmpty()) return CustomerFail(Error, LOCTEXT("EmptyNeeds", "普通书需求池不能为空。"));

    TArray<FName> TemplateIds;
    TMap<FName, float> PatienceById;
    // Reserve at most one visitor per listed copy when creating a daytime queue.
    // Unlisted initial stock never attracts an unserviceable first-day Secret visitor.
    int64 RemainingSecretStock = 0;
    if (Rules.bDaytimeOnlyLoop)
        for (const auto& Pair : Catalog.Books)
        {
            if (Pair.Value.Layer != EBookLayer::Inside || Pair.Value.BookType != EBookType::Secret) continue;
            const FBookRuntime* Book = State.Inventory.Find(Pair.Key);
            if (Book && ShopEconomy::IsSecretInventoryValid(*Book)) RemainingSecretStock += Book->ListedCopies;
        }
    double TotalWeight = 0.0;
    for (const auto& Pair : Catalog.Customers)
    {
        const FCustomerData& Data = Pair.Value;
        if (Data.Kind != ECustomerKind::Normal && Data.Kind != ECustomerKind::Hurry && Data.Kind != ECustomerKind::Secret && Data.Kind != ECustomerKind::Polluted)
            return CustomerFail(Error, LOCTEXT("InvalidKind", "顾客类型配置无效。"));
        if (!std::isfinite(Data.SpawnWeight) || Data.SpawnWeight < 0.f || Data.MinPollution < 0 || Data.MaxPollution < Data.MinPollution)
            return CustomerFail(Error, LOCTEXT("InvalidTemplate", "顾客生成权重或污染范围无效。"));
        if (Data.SpawnWeight == 0.f || State.Pollution < Data.MinPollution || State.Pollution > Data.MaxPollution) continue;
        // In the daytime-only loop, listed stock admits Secret visitors to the daytime shop.
        // The legacy flow keeps them exclusive to nighttime trading.
        if (Data.Kind == ECustomerKind::Secret &&
            (Rules.bDaytimeOnlyLoop ? RemainingSecretStock <= 0 : !bNight)) continue;
        if (Data.Kind == ECustomerKind::Polluted && State.Pollution < Rules.MediumThreshold) continue;
        if (!std::isfinite(Data.PatienceSeconds) || (Data.PatienceSeconds < 0.f && Data.PatienceSeconds != -1.f))
            return CustomerFail(Error, LOCTEXT("InvalidPatience", "顾客耐心配置无效。"));
        const double Patience = Data.PatienceSeconds == -1.f ? RulePatience(Rules, Data.Kind) : Data.PatienceSeconds;
        if (!std::isfinite(Patience) || Patience <= 0.0 || Patience > MAX_flt || static_cast<float>(Patience) <= 0.f)
            return CustomerFail(Error, LOCTEXT("InvalidPatience", "顾客耐心配置无效。"));
        TemplateIds.Add(Pair.Key);
        PatienceById.Add(Pair.Key, static_cast<float>(Patience));
    }
    if (TemplateIds.IsEmpty()) return CustomerFail(Error, LOCTEXT("NoTemplates", "当前时段和污染程度没有可生成的顾客模板。"));
    TemplateIds.Sort(FNameLexicalLess());
    // Sum in the same stable order used by the weighted draw.
    for (FName Id : TemplateIds) TotalWeight += Catalog.Customers.FindChecked(Id).SpawnWeight;
    if (!std::isfinite(TotalWeight) || TotalWeight <= 0.0)
        return CustomerFail(Error, LOCTEXT("WeightOverflow", "顾客权重合计无效。"));

    TArray<FCustomerRuntime> Customers;
    Customers.Reserve(static_cast<int32>(Count));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const auto IsAvailable = [&](FName Id)
        {
            const ECustomerKind Kind = Catalog.Customers.FindChecked(Id).Kind;
            if (Rules.bDaytimeOnlyLoop && RemainingSecretStock <= 0 && Kind == ECustomerKind::Secret) return false;
            return !Rules.bUniqueDailyCustomerPortraits ||
                ChoosePortrait(Kind, State.Day, Index, Id, Customers) != INDEX_NONE;
        };
        double AvailableWeight = TotalWeight;
        if (Rules.bUniqueDailyCustomerPortraits || (Rules.bDaytimeOnlyLoop && RemainingSecretStock <= 0))
        {
            AvailableWeight = 0.0;
            for (FName Id : TemplateIds)
                if (IsAvailable(Id))
                    AvailableWeight += Catalog.Customers.FindChecked(Id).SpawnWeight;
        }
        if (AvailableWeight <= 0.0)
            return CustomerFail(Error, LOCTEXT("NoAvailableTemplates", "没有满足库存及立绘限制的顾客模板；请保留可生成的普通顾客。"));
        const double Pick = static_cast<double>(Random.FRand()) * AvailableWeight;
        double RunningWeight = 0.0;
        FName Chosen = NAME_None;
        for (FName Id : TemplateIds)
        {
            if (!IsAvailable(Id)) continue;
            Chosen = Id;
            RunningWeight += Catalog.Customers.FindChecked(Id).SpawnWeight;
            if (Pick < RunningWeight) break;
        }
        const FCustomerData& Data = Catalog.Customers.FindChecked(Chosen);
        if (Rules.bDaytimeOnlyLoop && Data.Kind == ECustomerKind::Secret) --RemainingSecretStock;
        FCustomerRuntime Customer;
        Customer.TemplateId = Chosen;
        // Layer remains the requested product's origin, never the customer's location.
        Customer.NeedType = Data.Kind == ECustomerKind::Secret ? EBookType::Secret : Needs[Random.RandRange(0, Needs.Num() - 1)];
        Customer.NeedLayer = Data.Kind == ECustomerKind::Secret ? EBookLayer::Inside : EBookLayer::Table;
        Customer.Kind = Data.Kind;
        Customer.PortraitSlot = ChoosePortrait(Data.Kind, State.Day, Index, Chosen,
            Rules.bUniqueDailyCustomerPortraits ? Customers : TArray<FCustomerRuntime>());
        Customer.bPolluted = Data.Kind == ECustomerKind::Polluted;
        Customer.bSecret = Data.Kind == ECustomerKind::Secret;
        Customer.MaxPatience = PatienceById.FindChecked(Chosen);
        Customer.Patience = Customer.MaxPatience;
        Customers.Add(Customer);
    }
    State.Customers = MoveTemp(Customers);
    State.ActiveCustomer = INDEX_NONE;
    State.Random = Random;
    ResetArrival(State, Rules);
    return true;
}

int32 ShopCustomers::Current(const FShopRunState& State)
{
    for (int32 Index = 0; Index < State.Customers.Num(); ++Index)
        if (!State.Customers[Index].bServed) return Index;
    return INDEX_NONE;
}

bool ShopCustomers::AllServed(const FShopRunState& State)
{
    return Current(State) == INDEX_NONE;
}

void ShopCustomers::ResetArrival(FShopRunState& State, const FRunRules& Rules)
{
    State.CustomerArrivalRemaining = 0.f;
    State.bCustomerPresent = Current(State) != INDEX_NONE;
    if (State.bCustomerPresent && Rules.bDaytimeOnlyLoop && Rules.bUseCustomerArrivalDelay)
    {
        State.CustomerArrivalRemaining = State.CosmeticRandom.FRandRange(Rules.CustomerArrivalMin, Rules.CustomerArrivalMax);
        State.bCustomerPresent = State.CustomerArrivalRemaining <= 0.f;
    }
}

bool ShopCustomers::Complete(FShopRunState& State, int32 Index, EShopActionResult Resolution)
{
    if (Index == INDEX_NONE || Index != Current(State)) return false;
    if (Resolution != EShopActionResult::Sold && Resolution != EShopActionResult::WrongBook && Resolution != EShopActionResult::NoMatch && Resolution != EShopActionResult::Expired && Resolution != EShopActionResult::Cancelled && Resolution != EShopActionResult::Rejected) return false;
    State.Customers[Index].bServed = true;
    State.Customers[Index].Resolution = Resolution;
    State.ActiveCustomer = INDEX_NONE;
    return true;
}

int32 ShopCustomers::AdvancePatience(FShopRunState& State, const FRunRules& Rules, float DeltaSeconds)
{
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.f) return INDEX_NONE;
    if (Rules.bDaytimeOnlyLoop && Rules.bUseCustomerArrivalDelay && !State.bCustomerPresent) return INDEX_NONE;
    if (!std::isfinite(Rules.PatienceDropRatePolluted) || Rules.PatienceDropRatePolluted < 0.f) return INDEX_NONE;
    const int32 Index = Current(State);
    if (Index == INDEX_NONE) return INDEX_NONE;
    FCustomerRuntime& Customer = State.Customers[Index];
    if (!std::isfinite(Customer.Patience)) return INDEX_NONE;
    // Evaluate pollution now, not at spawn, so purifying immediately restores the normal rate.
    const double Rate = State.Pollution >= Rules.MediumThreshold ? 1.0 + Rules.PatienceDropRatePolluted : 1.0;
    const double Elapsed = static_cast<double>(DeltaSeconds) * Rate;
    if (Customer.Patience > Elapsed)
    {
        Customer.Patience = static_cast<float>(Customer.Patience - Elapsed);
        return INDEX_NONE;
    }
    Customer.Patience = 0.f;
    Complete(State, Index, EShopActionResult::Expired);
    // Excess elapsed time is intentionally not transferred to the next queued customer.
    return Index;
}

int32 ShopCustomers::AdvancePatience(FShopRunState& State, float DeltaSeconds)
{
    return AdvancePatience(State, FRunRules(), DeltaSeconds);
}

FText ShopCustomers::BuildNeedText(const FCustomerRuntime& Customer, const FShopCatalog& Catalog)
{
    const FCustomerData* Data = Catalog.Customers.Find(Customer.TemplateId);
    if (!Data) return LOCTEXT("MissingCustomer", "顾客模板不存在");
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("BookType"), TypeName(Customer.NeedType));
    Arguments.Add(TEXT("NeedType"), TypeName(Customer.NeedType));
    Arguments.Add(TEXT("Type"), TypeName(Customer.NeedType));
    Arguments.Add(TEXT("类型"), TypeName(Customer.NeedType));
    Arguments.Add(TEXT("Name"), Data->DisplayName);
    const FText Pattern = Data->NeedLine.IsEmpty() ? LOCTEXT("DefaultNeed", "我想找一本{BookType}。") : Data->NeedLine;
    return FText::Format(Pattern, Arguments);
}

#undef LOCTEXT_NAMESPACE
