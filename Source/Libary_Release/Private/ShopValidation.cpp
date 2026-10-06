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
            if ((Effect.Type == EShopEffectType::DestroySecretBooks || Effect.Type == EShopEffectType::AlterSecretBook ||
                Effect.Type == EShopEffectType::UnlockSecretBook || Effect.Type == EShopEffectType::ReturnLostSecretBook) && !Effect.TargetId.IsNone())
            {
                const FBookData* Book = C.Books.Find(Effect.TargetId);
                if (!Book || Book->Layer != EBookLayer::Inside) return Fail(Error, Row + TEXT(": effect references a missing/non-secret book"));
            }
            if (Effect.Type == EShopEffectType::AddClue && Effect.TargetId.IsNone()) return Fail(Error, Row + TEXT(": AddClue needs TargetId"));
            if (Effect.Type == EShopEffectType::DestroySecretBooks && Effect.Amount < 0) return Fail(Error, Row + TEXT(": DestroySecretBooks Amount must be nonnegative"));
            if (Effect.Type == EShopEffectType::UnlockSecretBook && Effect.Amount < 0) return Fail(Error, Row + TEXT(": UnlockSecretBook Amount must be nonnegative"));
            if ((Effect.Type == EShopEffectType::SpawnFakeCustomer || Effect.Type == EShopEffectType::SkipNightDecay ||
                Effect.Type == EShopEffectType::NextPollutionBonus || Effect.Type == EShopEffectType::ReturnLostSecretBook ||
                Effect.Type == EShopEffectType::BlockLightSpread) && (Effect.Amount < 0 || Effect.Amount > 10000))
                return Fail(Error, Row + TEXT(": effect count must be between 0 and 10000"));
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
    if (R.PsychicMax < 1 || R.StartPsychic > R.PsychicMax || R.StartPollution >= R.PollutionLimit ||
        !FMath::IsFinite(R.PatienceDropRatePolluted) || R.PatienceDropRatePolluted < 0.f || R.PatienceDropRatePolluted > 10.f)
        return Fail(Error, TEXT("DT_RunRules.Default: invalid psychic cap, initial pollution or patience drain rate"));
    const int32 NonnegativeRules[] = { R.EmergencyPollutionCut, R.EmergencyMoneyCost, R.LightSpreadPerNight,
        R.FakeCustomerPollutionPerSecond, R.FakeCustomerMaxPollution, R.AlteredBookReadPollution,
        R.ReturnedBookReadPollution, R.ReturnedBookSellPollution, R.ReturnedBookFallbackPollution,
        R.PermanentRentPenalty, R.PermanentCustomerPenalty, R.MaxRentPenalty, R.MaxCustomerPenalty, R.DecreeCooldownTurns };
    for (int32 Value : NonnegativeRules) if (Value < 0 || Value > 100000)
        return Fail(Error, TEXT("DT_RunRules.Default: invalid decree consequence or cooldown"));
    if (static_cast<int64>(R.Rent) + R.MaxRentPenalty > MAX_int32 || R.EmergencyPollutionCut <= 0)
        return Fail(Error, TEXT("DT_RunRules.Default: rent overflow or nonpositive emergency reduction"));
    const int32 CandidateCounts[] = {R.DecreeCandidateCountLight, R.DecreeCandidateCountMedium, R.DecreeCandidateCountHeavy};
    for (int32 Count : CandidateCounts) if (Count < 1 || Count > 100)
        return Fail(Error, TEXT("DT_RunRules.Default: invalid per-stage candidate count"));
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
            !StaticEnum<EBookLayer>()->IsValidEnumValue(static_cast<int64>(B.Layer)) || B.Cost < 0 || B.Price < 0 || B.InitialStock < 0 || B.InitialStock > 10000 ||
            B.InitialOwnedStock < 0 || B.CollectOfferPerNight < 0 || B.CollectOfferPerNight > 10000 ||
            B.PsychicYield < -1 || B.PollutionYield < -1 || B.EnlightenYield < 0 || B.SaleEnlightenYield < 0 ||
            !FMath::IsFinite(B.SaleEnlightenChance) || B.SaleEnlightenChance < 0.f || B.SaleEnlightenChance > 1.f || B.SealLevel < 0)
            return Fail(Error, Row + TEXT(": invalid identity, type, price, yield or stock"));
        if (B.Layer == EBookLayer::Table) { ++TableCount; if (B.Cost <= 0 || B.BookType == EBookType::Secret) return Fail(Error, Row + TEXT(": surface book requires positive purchase cost and a surface category")); }
        else { ++InsideCount; if (B.BookType != EBookType::Secret) return Fail(Error, Row + TEXT(": this service contract uses Secret demand for inside books")); }
        Stock += B.InitialStock;
        for (FName Clue : B.CluePool) if (Clue.IsNone()) return Fail(Error, Row + TEXT(": CluePool contains None"));
    }
    if (Stock > 100000 || TableCount == 0 || InsideCount == 0) return Fail(Error, TEXT("DT_Books: at most 10000 copies per row / 100000 total, and both Table/Inside layers are required"));
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
            if (Customer.MinPollution == 0 && Customer.MaxPollution >= R.PollutionLimit - 1)
            {
                HasInsideCustomer = true;
                if (Customer.Kind == ECustomerKind::Normal || Customer.Kind == ECustomerKind::Hurry) HasSurfaceCustomer = true;
            }
        }
    }
    if (!HasSurfaceCustomer || (R.InsideCustomers > 0 && !HasInsideCustomer)) return Fail(Error, TEXT("DT_Customers: need an always-eligible Normal/Hurry surface template and an eligible inside template"));
    for (const auto& Pair : C.Decrees)
    {
        const FDecreeData& D = Pair.Value;
        const FString Row = TEXT("DT_Decrees.") + Pair.Key.ToString();
        if ((!D.Id.IsNone() && D.Id != Pair.Key) || D.DisplayName.IsEmpty() || !StaticEnum<EDecreeQuality>()->IsValidEnumValue(static_cast<int64>(D.Quality)) ||
            D.PsychicCost < 0 || D.PollutionCut < 0 || D.LoopholeDelay < 0 || D.LoopholeDelay > 100000 || D.CooldownTurns < 0 || D.CooldownTurns > 100000 ||
            !StaticEnum<EPollutionStage>()->IsValidEnumValue(static_cast<int64>(D.MinStage)) ||
            !StaticEnum<EPollutionStage>()->IsValidEnumValue(static_cast<int64>(D.MaxStage)) || D.MinStage > D.MaxStage ||
            D.bFallback || Pair.Key == FName(TEXT("emergency_calm")))
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
    TSet<EShopEnding> EndingKinds;
    TSet<EShopEndingCondition> EndingConditions;
    TSet<int32> Priorities;
    int32 FallbackPriority = -1;
    for (const auto& Pair : C.Endings)
    {
        const FEndingData& E = Pair.Value;
        if (Pair.Key.IsNone() || E.Ending == EShopEnding::None ||
            !StaticEnum<EShopEnding>()->IsValidEnumValue(static_cast<int64>(E.Ending)) ||
            !StaticEnum<EShopEndingCondition>()->IsValidEnumValue(static_cast<int64>(E.Condition)) ||
            E.Priority < 0 || E.Title.IsEmpty() || E.Text.IsEmpty() ||
            E.NegativeDaysRequired < 1 || E.PollutionThreshold < 1 || E.MinEnlighten < 0 || E.MaxPollutionExclusive < 1 || E.MinMoney < 0 ||
            EndingKinds.Contains(E.Ending) || EndingConditions.Contains(E.Condition) || Priorities.Contains(E.Priority))
            return Fail(Error, TEXT("DT_Endings: invalid or duplicate ending, condition, priority, text or thresholds"));
        if (E.Condition == EShopEndingCondition::PollutionLimit && E.PollutionThreshold != R.PollutionLimit)
            return Fail(Error, TEXT("DT_Endings: pollution threshold must match RunRules.PollutionLimit"));
        EndingKinds.Add(E.Ending); EndingConditions.Add(E.Condition); Priorities.Add(E.Priority);
        if (E.Condition == EShopEndingCondition::FinalFallback) FallbackPriority = E.Priority;
    }
    if (!C.Endings.IsEmpty())
    {
        if (C.Endings.Num() != 4 || FallbackPriority < 0) return Fail(Error, TEXT("DT_Endings: all four ending conditions are required"));
        for (int32 Priority : Priorities) if (Priority > FallbackPriority) return Fail(Error, TEXT("DT_Endings: fallback must have the last priority"));
    }
    if (R.bRequireCoreContentCounts && (TableCount != 9 || InsideCount != 7 || Kinds.Num() != 4 || C.Decrees.Num() != 7 || EnabledDecrees != 7 || C.Endings.Num() != 4))
        return Fail(Error, TEXT("Core validation requires 9 surface + 7 inside books, 4 customer kinds, 7 enabled decrees and 4 endings; history may be deferred"));
    if (R.bRequireFinalContentCounts && (TableCount != 9 || InsideCount != 7 || Kinds.Num() != 4 || C.Decrees.Num() != 7 || EnabledDecrees != 7 || C.Events.IsEmpty()))
        return Fail(Error, TEXT("Final content validation requires 9 surface + 7 inside books, 4 customer kinds, 7 enabled decrees and authored events"));
    return true;
}
