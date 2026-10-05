#include "ShopValidation.h"

namespace
{
    bool Fail(FText& Error, const FString& Message) { Error = FText::FromString(Message); return false; }

    bool EffectsValid(const TArray<FShopEffect>& Effects, const FShopCatalog& C, const FString& Row, FText& Error)
    {
        for (const FShopEffect& Effect : Effects)
        {
            if (!StaticEnum<EShopEffectType>()->IsValidEnumValue(static_cast<int64>(Effect.Type)) ||
                !FMath::IsFinite(Effect.Multiplier) || Effect.Multiplier < 0.f || Effect.DurationTurns < -1 || Effect.DurationTurns > 100000)
                return Fail(Error, Row + TEXT(": invalid effect type, multiplier or duration"));
            if ((Effect.Type == EShopEffectType::DestroySecretBooks || Effect.Type == EShopEffectType::AlterSecretBook) && !Effect.TargetId.IsNone())
            {
                const FBookData* Book = C.Books.Find(Effect.TargetId);
                if (!Book || Book->Layer != EBookLayer::Inside) return Fail(Error, Row + TEXT(": effect references a missing/non-secret book"));
            }
            if (Effect.Type == EShopEffectType::AddClue && Effect.TargetId.IsNone()) return Fail(Error, Row + TEXT(": AddClue needs TargetId"));
            if (Effect.Type == EShopEffectType::DestroySecretBooks && Effect.Amount < 0) return Fail(Error, Row + TEXT(": DestroySecretBooks Amount must be nonnegative"));
            if (Effect.Type == EShopEffectType::BlockLightSpread) return Fail(Error, Row + TEXT(": BlockLightSpread needs an authored spread rule before it can be enabled"));
        }
        return true;
    }
}

bool ShopValidation::Validate(const FShopCatalog& C, FText& Error)
{
    Error = FText();
    const FRunRules& R = C.Rules;
    if (R.StartMoney < 0 || R.StartPsychic < 0 || R.StartPollution < 0 || R.StartEnlighten < 0 || R.MaxDays < 1 || R.MaxDays > 100000 ||
        R.Rent < 0 || R.NegativeDaysToClose < 1 || R.DaysPerWeek < 1 || R.RedeemTarget < 0 ||
        R.EnlightenWin < 0 || R.WinPollutionThreshold < 1 || R.WinPollutionThreshold > R.PollutionLimit)
        return Fail(Error, TEXT("DT_RunRules.Default: invalid start resources, days, rent or ending rules"));
    if (!(0 < R.LightThreshold && R.LightThreshold < R.MediumThreshold && R.MediumThreshold < R.HeavyThreshold && R.HeavyThreshold < R.PollutionLimit))
        return Fail(Error, TEXT("DT_RunRules.Default: pollution thresholds must satisfy 0 < Light < Medium < Heavy < Limit"));
    if (R.CustomersMin < 0 || R.CustomersMax < R.CustomersMin || R.CustomersMax > 10000 || R.InsideCustomers < 0 || R.InsideCustomers > 10000 ||
        R.WeekTwoCustomerBonus < 0 || R.WeekTwoCustomerBonus > 10000 || R.RandomNeedPool.IsEmpty())
        return Fail(Error, TEXT("DT_RunRules.Default: invalid customer counts or empty RandomNeedPool"));
    for (const EBookType Type : R.RandomNeedPool)
        if (!StaticEnum<EBookType>()->IsValidEnumValue(static_cast<int64>(Type)) || Type == EBookType::Secret)
            return Fail(Error, TEXT("DT_RunRules.Default.RandomNeedPool: surface needs must be valid non-secret book types"));
    const float Values[] = {R.PatienceNormal, R.PatienceHurry, R.PatienceSecret, R.PatiencePolluted, R.PollutedPatienceMultiplier};
    for (float Value : Values) if (!FMath::IsFinite(Value) || Value <= 0.f) return Fail(Error, TEXT("DT_RunRules.Default: patience and multiplier must be finite and positive"));
    if (!FMath::IsFinite(R.ClueDropChance) || R.ClueDropChance < 0.f || R.ClueDropChance > 1.f || R.PollutionDecay < 0 ||
        R.PollutionOnRead < 0 || R.PollutionOnCollect < 0 || R.PollutionOnSell < 0 || R.PollutionOnHistory < 0 || R.PollutionOnPollutedCustomer < 0 ||
        R.ReadPsychicGain < 0 || R.CollectPsychicCost < 0 || R.DecreeCandidateCount < 1 || R.DecreeCandidateCount > 100 ||
        R.LoopholeDelayTurns < 1 || R.LoopholeDelayTurns > 100000 || R.HeavyGraceTurns < 1 || R.HeavyGraceTurns > 100000 || R.HeavyPenalty < 0 ||
        !StaticEnum<ERentTiming>()->IsValidEnumValue(static_cast<int64>(R.RentTiming)))
        return Fail(Error, TEXT("DT_RunRules.Default: invalid pollution, decree, resource, probability or rent-timing rule"));
    if (C.Books.IsEmpty() || C.Customers.IsEmpty() || C.Decrees.IsEmpty()) return Fail(Error, TEXT("Books, Customers and Decrees tables must not be empty"));
    int64 Stock = 0;
    int32 TableCount = 0, InsideCount = 0, EnabledDecrees = 0;
    for (const auto& Pair : C.Books)
    {
        const FBookData& B = Pair.Value;
        const FString Row = TEXT("DT_Books.") + Pair.Key.ToString();
        if (Pair.Key.IsNone() || B.DisplayName.IsEmpty() || !StaticEnum<EBookType>()->IsValidEnumValue(static_cast<int64>(B.BookType)) ||
            !StaticEnum<EBookLayer>()->IsValidEnumValue(static_cast<int64>(B.Layer)) || B.Cost < 0 || B.Price < 0 || B.InitialStock < 0 ||
            B.InitialOwnedStock < 0 || B.PsychicYield < -1 || B.PollutionYield < -1 || B.EnlightenYield < 0 || B.SealLevel < 0)
            return Fail(Error, Row + TEXT(": invalid identity, type, price, yield or stock"));
        if (B.Layer == EBookLayer::Table) { ++TableCount; if (B.Cost <= 0 || B.BookType == EBookType::Secret) return Fail(Error, Row + TEXT(": surface book requires positive purchase cost and a surface category")); }
        else { ++InsideCount; if (B.BookType != EBookType::Secret) return Fail(Error, Row + TEXT(": this service contract uses Secret demand for inside books")); }
        Stock += static_cast<int64>(B.InitialStock) + B.InitialOwnedStock;
        for (FName Clue : B.CluePool) if (Clue.IsNone()) return Fail(Error, Row + TEXT(": CluePool contains None"));
    }
    if (Stock > MAX_int32 || TableCount == 0 || InsideCount == 0) return Fail(Error, TEXT("DT_Books: total stock overflow or missing Table/Inside layer"));
    bool HasSurfaceCustomer = false, HasInsideCustomer = false;
    TSet<ECustomerKind> Kinds;
    for (const auto& Pair : C.Customers)
    {
        const FCustomerData& Customer = Pair.Value;
        if (Pair.Key.IsNone() || Customer.DisplayName.IsEmpty() || !StaticEnum<ECustomerKind>()->IsValidEnumValue(static_cast<int64>(Customer.Kind)) ||
            !FMath::IsFinite(Customer.SpawnWeight) || Customer.SpawnWeight < 0.f || !FMath::IsFinite(Customer.PatienceSeconds) ||
            (Customer.PatienceSeconds <= 0.f && Customer.PatienceSeconds != -1.f) || Customer.MinPollution < 0 || Customer.MaxPollution < Customer.MinPollution)
            return Fail(Error, TEXT("DT_Customers.") + Pair.Key.ToString() + TEXT(": invalid customer configuration"));
        Kinds.Add(Customer.Kind);
        if (Customer.SpawnWeight > 0.f)
        {
            if (Customer.Kind == ECustomerKind::Secret && Customer.MinPollution == 0 && Customer.MaxPollution >= R.PollutionLimit - 1) HasInsideCustomer = true;
            else if (Customer.Kind != ECustomerKind::Polluted && Customer.MinPollution == 0 && Customer.MaxPollution >= R.PollutionLimit - 1) HasSurfaceCustomer = true;
        }
    }
    if (!HasSurfaceCustomer || (R.InsideCustomers > 0 && !HasInsideCustomer)) return Fail(Error, TEXT("DT_Customers: need an always-eligible surface template and an inside Secret template"));
    for (const auto& Pair : C.Decrees)
    {
        const FDecreeData& D = Pair.Value;
        const FString Row = TEXT("DT_Decrees.") + Pair.Key.ToString();
        if ((!D.Id.IsNone() && D.Id != Pair.Key) || D.DisplayName.IsEmpty() || !StaticEnum<EDecreeQuality>()->IsValidEnumValue(static_cast<int64>(D.Quality)) ||
            D.PsychicCost < 0 || D.PollutionCut < 0 || D.LoopholeDelay < 0 || D.LoopholeDelay > 100000 || D.CooldownTurns < 0 || D.CooldownTurns > 100000)
            return Fail(Error, Row + TEXT(": invalid decree identity, cost or timing"));
        if (!EffectsValid(D.CostEffect, C, Row, Error) || !EffectsValid(D.Effects, C, Row, Error) || !EffectsValid(D.LoopholeEffect, C, Row, Error)) return false;
        if (D.bEnabled) ++EnabledDecrees;
    }
    if (EnabledDecrees == 0) return Fail(Error, TEXT("DT_Decrees: at least one enabled decree is required"));
    for (const auto& Pair : C.Events)
    {
        const FEventData& E = Pair.Value;
        const FString Row = TEXT("DT_Events.") + Pair.Key.ToString();
        if ((!E.Id.IsNone() && E.Id != Pair.Key) || !StaticEnum<EShopEventTrigger>()->IsValidEnumValue(static_cast<int64>(E.Trigger)) ||
            E.MinDay < 1 || E.MaxDay < E.MinDay || !FMath::IsFinite(E.Chance) || E.Chance < 0.f || E.Chance > 1.f ||
            (!E.RequiredBookId.IsNone() && !C.Books.Contains(E.RequiredBookId))) return Fail(Error, Row + TEXT(": invalid event identity, trigger or probability"));
        if (!EffectsValid(E.ResultA, C, Row, Error)) return false;
    }
    for (const auto& Pair : C.MarketItems)
    {
        const FMarketItemData& M = Pair.Value;
        const FString Row = TEXT("DT_MarketItems.") + Pair.Key.ToString();
        if ((!M.Id.IsNone() && M.Id != Pair.Key) || M.Price < 0 || M.Week < 0 || M.DisplayName.IsEmpty()) return Fail(Error, Row + TEXT(": invalid market item"));
        if (!EffectsValid(M.TabooCost, C, Row, Error) || !EffectsValid(M.Effect, C, Row, Error) || !EffectsValid(M.SideEffect, C, Row, Error)) return false;
    }
    if (R.bEnableMarket && C.MarketItems.IsEmpty()) return Fail(Error, TEXT("Market enabled but DT_MarketItems is empty"));
    TSet<int32> GuideIndices;
    for (const auto& Pair : C.OwlLines)
    {
        const FOwlLine& Line = Pair.Value;
        if ((!Line.Id.IsNone() && Line.Id != Pair.Key) || Line.GuideIndex < 0 || Line.GuideIndex > 10 || Line.Text.IsEmpty()) return Fail(Error, TEXT("DT_Owl: invalid line identity, text or guide index"));
        if (Line.GuideIndex > 0) { if (GuideIndices.Contains(Line.GuideIndex)) return Fail(Error, TEXT("DT_Owl: duplicate guide index")); GuideIndices.Add(Line.GuideIndex); }
    }
    if (!C.OwlLines.IsEmpty() && GuideIndices.Num() != 10) return Fail(Error, TEXT("DT_Owl: provide all ten ordered guide lines, or leave the optional table empty"));
    if (R.bRequireFinalContentCounts && (TableCount != 9 || InsideCount != 7 || Kinds.Num() != 4 || C.Decrees.Num() != 7 || EnabledDecrees != 7 || C.Events.IsEmpty()))
        return Fail(Error, TEXT("Final content validation requires 9 surface + 7 inside books, 4 customer kinds, 7 enabled decrees and authored events"));
    return true;
}
