#include "ShopCustomers.h"

#include <cmath>

#define LOCTEXT_NAMESPACE "ShopCustomers"

namespace
{
    bool Fail(FText& Error, const FText& Message)
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

bool ShopCustomers::Generate(FShopRunState& State, const FShopCatalog& Catalog, EBookLayer Layer, FText& Error)
{
    Error = FText::GetEmpty();
    const FRunRules& Rules = Catalog.Rules;
    if (Layer != EBookLayer::Table && Layer != EBookLayer::Inside)
        return Fail(Error, LOCTEXT("InvalidLayer", "顾客所属世界无效。"));
    if (Rules.CustomersMin < 0 || Rules.CustomersMax < Rules.CustomersMin || Rules.CustomersMax > 10000 || Rules.InsideCustomers < 0 || Rules.InsideCustomers > 10000 || Rules.DaysPerWeek <= 0)
        return Fail(Error, LOCTEXT("InvalidCounts", "每日顾客数量或每周天数配置无效。"));

    // Generate with a copied stream; configuration failures must not consume randomness.
    FRandomStream Random = State.Random;
    int64 Count = Layer == EBookLayer::Inside ? Rules.InsideCustomers : Random.RandRange(Rules.CustomersMin, Rules.CustomersMax);
    if (Layer == EBookLayer::Table && State.Day > Rules.DaysPerWeek) Count += Rules.WeekTwoCustomerBonus;
    for (const FShopModifier& Modifier : State.Modifiers)
        if (Modifier.Type == EShopEffectType::CustomerCountDelta && (Modifier.EndTurn < 0 || State.Turn < Modifier.EndTurn)) Count += Modifier.Amount;
    Count = FMath::Max<int64>(0, Count);
    if (Count > 10000) return Fail(Error, LOCTEXT("TooManyCustomers", "单批顾客数量超过支持的 10000 人。"));
    if (Count == 0)
    {
        State.Customers.Reset();
        State.ActiveCustomer = INDEX_NONE;
        State.Random = Random;
        return true;
    }

    TArray<EBookType> Needs;
    if (Layer == EBookLayer::Inside) Needs.Add(EBookType::Secret);
    else
    {
        for (EBookType Type : Rules.RandomNeedPool)
        {
            if (!ValidTableNeed(Type)) return Fail(Error, LOCTEXT("InvalidNeed", "表世界顾客需求池只能包含小说、诗歌和历史。"));
            Needs.Add(Type);
        }
        if (Needs.IsEmpty()) return Fail(Error, LOCTEXT("EmptyNeeds", "表世界顾客需求池不能为空。"));
    }

    TArray<FName> TemplateIds;
    TMap<FName, float> PatienceById;
    double TotalWeight = 0.0;
    for (const auto& Pair : Catalog.Customers)
    {
        const FCustomerData& Data = Pair.Value;
        if (Data.Kind != ECustomerKind::Normal && Data.Kind != ECustomerKind::Hurry && Data.Kind != ECustomerKind::Secret && Data.Kind != ECustomerKind::Polluted)
            return Fail(Error, LOCTEXT("InvalidKind", "顾客类型配置无效。"));
        if (!std::isfinite(Data.SpawnWeight) || Data.SpawnWeight < 0.f || Data.MinPollution < 0 || Data.MaxPollution < Data.MinPollution)
            return Fail(Error, LOCTEXT("InvalidTemplate", "顾客生成权重或污染范围无效。"));
        if (Data.SpawnWeight == 0.f || State.Pollution < Data.MinPollution || State.Pollution > Data.MaxPollution) continue;
        if ((Layer == EBookLayer::Inside) != (Data.Kind == ECustomerKind::Secret)) continue;
        if (Data.Kind == ECustomerKind::Polluted && State.Pollution < Rules.MediumThreshold) continue;
        if (!std::isfinite(Data.PatienceSeconds) || (Data.PatienceSeconds < 0.f && Data.PatienceSeconds != -1.f))
            return Fail(Error, LOCTEXT("InvalidPatience", "顾客耐心配置无效。"));
        double Patience = Data.PatienceSeconds == -1.f ? RulePatience(Rules, Data.Kind) : Data.PatienceSeconds;
        if (State.Pollution >= Rules.MediumThreshold)
        {
            if (!std::isfinite(Rules.PollutedPatienceMultiplier) || Rules.PollutedPatienceMultiplier <= 0.f)
                return Fail(Error, LOCTEXT("InvalidPatienceMultiplier", "污染耐心倍率必须为有限正数。"));
            Patience *= Rules.PollutedPatienceMultiplier;
        }
        if (!std::isfinite(Patience) || Patience <= 0.0 || Patience > MAX_flt || static_cast<float>(Patience) <= 0.f)
            return Fail(Error, LOCTEXT("InvalidPatience", "顾客耐心配置无效。"));
        TemplateIds.Add(Pair.Key);
        PatienceById.Add(Pair.Key, static_cast<float>(Patience));
    }
    if (TemplateIds.IsEmpty()) return Fail(Error, LOCTEXT("NoTemplates", "当前世界和污染程度没有可生成的顾客模板。"));
    TemplateIds.Sort(FNameLexicalLess());
    // Sum in the same stable order used by the weighted draw.
    for (FName Id : TemplateIds) TotalWeight += Catalog.Customers.FindChecked(Id).SpawnWeight;
    if (!std::isfinite(TotalWeight) || TotalWeight <= 0.0)
        return Fail(Error, LOCTEXT("WeightOverflow", "顾客权重合计无效。"));

    TArray<FCustomerRuntime> Customers;
    Customers.Reserve(static_cast<int32>(Count));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const double Pick = static_cast<double>(Random.FRand()) * TotalWeight;
        double RunningWeight = 0.0;
        FName Chosen = TemplateIds.Last();
        for (FName Id : TemplateIds)
        {
            RunningWeight += Catalog.Customers.FindChecked(Id).SpawnWeight;
            if (Pick < RunningWeight) { Chosen = Id; break; }
        }
        const FCustomerData& Data = Catalog.Customers.FindChecked(Chosen);
        FCustomerRuntime Customer;
        Customer.TemplateId = Chosen;
        Customer.NeedType = Needs[Random.RandRange(0, Needs.Num() - 1)];
        Customer.NeedLayer = Layer;
        Customer.Kind = Data.Kind;
        Customer.bPolluted = Data.Kind == ECustomerKind::Polluted;
        Customer.bSecret = Data.Kind == ECustomerKind::Secret;
        Customer.MaxPatience = PatienceById.FindChecked(Chosen);
        Customer.Patience = Customer.MaxPatience;
        Customers.Add(Customer);
    }
    State.Customers = MoveTemp(Customers);
    State.ActiveCustomer = INDEX_NONE;
    State.Random = Random;
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

bool ShopCustomers::Complete(FShopRunState& State, int32 Index, EShopActionResult Resolution)
{
    if (Index == INDEX_NONE || Index != Current(State)) return false;
    if (Resolution != EShopActionResult::Sold && Resolution != EShopActionResult::WrongBook && Resolution != EShopActionResult::NoMatch && Resolution != EShopActionResult::Expired && Resolution != EShopActionResult::Cancelled && Resolution != EShopActionResult::Rejected) return false;
    State.Customers[Index].bServed = true;
    State.Customers[Index].Resolution = Resolution;
    State.ActiveCustomer = INDEX_NONE;
    return true;
}

int32 ShopCustomers::AdvancePatience(FShopRunState& State, float DeltaSeconds)
{
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.f) return INDEX_NONE;
    const int32 Index = Current(State);
    if (Index == INDEX_NONE) return INDEX_NONE;
    FCustomerRuntime& Customer = State.Customers[Index];
    if (!std::isfinite(Customer.Patience)) return INDEX_NONE;
    if (Customer.Patience > DeltaSeconds)
    {
        Customer.Patience -= DeltaSeconds;
        return INDEX_NONE;
    }
    Customer.Patience = 0.f;
    Complete(State, Index, EShopActionResult::Expired);
    // Excess elapsed time is intentionally not transferred to the next queued customer.
    return Index;
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
