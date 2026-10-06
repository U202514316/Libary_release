#include "ShopDecrees.h"

#include "ShopEconomy.h"

namespace
{
    const FName EmergencyId(TEXT("emergency_calm"));
    bool Fail(FText& Error, const TCHAR* Message)
    {
        Error = FText::FromString(Message);
        return false;
    }

    bool IsLiveModifier(const FShopModifier& Modifier, int32 Turn)
    {
        return Modifier.EndTurn == INDEX_NONE || Modifier.EndTurn > Turn;
    }

    bool IsPassive(EShopEffectType Type)
    {
        switch (Type)
        {
        case EShopEffectType::IncomeMultiplier:
        case EShopEffectType::PsychicGainMultiplier:
        case EShopEffectType::DecayMultiplier:
        case EShopEffectType::NightlyMoney:
        case EShopEffectType::NightlyPollution:
        case EShopEffectType::CustomerCountDelta:
        case EShopEffectType::BlockLightSpread:
        case EShopEffectType::NightIncomeMultiplier:
            return true;
        default:
            return false;
        }
    }

    bool IsMultiplier(EShopEffectType Type)
    {
        return Type == EShopEffectType::IncomeMultiplier
            || Type == EShopEffectType::PsychicGainMultiplier
            || Type == EShopEffectType::DecayMultiplier
            || Type == EShopEffectType::NightIncomeMultiplier;
    }

    bool ChangeNonnegativeResource(int32& Resource, int32 Amount, bool bIsCost, FText& Error)
    {
        const int64 Next = static_cast<int64>(Resource) + Amount;
        if (bIsCost && Amount < 0 && Next < 0)
        {
            return Fail(Error, TEXT("There are not enough resources to pay this effect."));
        }
        if (Next > MAX_int32)
        {
            return Fail(Error, TEXT("A resource effect exceeds the supported integer range."));
        }
        Resource = static_cast<int32>(FMath::Max<int64>(0, Next));
        return true;
    }

    bool CalculateEndTurn(const FShopRunState& State, const FShopEffect& Effect,
        int32 DefaultEndTurn, int32& OutEndTurn, FText& Error)
    {
        if (Effect.DurationTurns < -1 || DefaultEndTurn < -1)
        {
            return Fail(Error, TEXT("Effect duration must be -1, 0, or a positive logical turn count."));
        }
        if (Effect.DurationTurns > 0)
        {
            const int64 EndTurn = static_cast<int64>(State.Turn) + Effect.DurationTurns;
            if (State.Turn < 0 || EndTurn > MAX_int32)
            {
                return Fail(Error, TEXT("Effect duration exceeds the supported turn range."));
            }
            OutEndTurn = static_cast<int32>(EndTurn);
        }
        else
        {
            OutEndTurn = Effect.DurationTurns == -1 ? INDEX_NONE : DefaultEndTurn;
        }
        return true;
    }

    struct FCopyAddress
    {
        FName BookId;
        int32 CopyIndex = 0;
    };

    TArray<FCopyAddress> SecretCopies(FShopRunState& State, const FShopCatalog& Catalog,
        FName TargetId, bool bSealedOnly = false, bool bAlteredOnly = false)
    {
        TArray<FName> Ids;
        State.Inventory.GetKeys(Ids);
        Ids.Sort([](FName A, FName B) { return A.LexicalLess(B); });
        TArray<FCopyAddress> Result;
        for (FName Id : Ids)
        {
            const FBookData* Book = Catalog.Books.Find(Id);
            if (!Book || Book->Layer != EBookLayer::Inside || (!TargetId.IsNone() && Id != TargetId)) continue;
            FBookRuntime& Runtime = State.Inventory.FindChecked(Id);
            for (int32 Index = 0; Index < Runtime.SecretCopies.Num(); ++Index)
            {
                const FSecretBookCopy& Copy = Runtime.SecretCopies[Index];
                if ((!bSealedOnly || Copy.bSealed) && (!bAlteredOnly || Copy.bAltered))
                {
                    FCopyAddress Address;
                    Address.BookId = Id;
                    Address.CopyIndex = Index;
                    Result.Add(Address);
                }
            }
        }
        return Result;
    }

    bool ValidateSecretTarget(const FShopCatalog& Catalog, FName TargetId, FText& Error)
    {
        if (TargetId.IsNone())
        {
            return true;
        }
        const FBookData* Book = Catalog.Books.Find(TargetId);
        return (Book && Book->Layer == EBookLayer::Inside)
            || Fail(Error, TEXT("A secret-book effect references an unknown or surface book."));
    }

    bool DestroySecretCopies(FShopRunState& State, const FShopCatalog& Catalog,
        const FShopEffect& Effect, bool bIsCost, FText& Error)
    {
        if (Effect.Amount < 0)
        {
            return Fail(Error, TEXT("DestroySecretBooks requires a nonnegative copy count."));
        }
        if (!ValidateSecretTarget(Catalog, Effect.TargetId, Error))
        {
            return false;
        }
        TArray<FCopyAddress> Copies = SecretCopies(State, Catalog, Effect.TargetId);
        if (bIsCost && Copies.Num() < Effect.Amount)
        {
            return Fail(Error, TEXT("There are not enough owned secret books to pay this cost."));
        }
        const int32 CopiesToDestroy = FMath::Min(Copies.Num(), Effect.Amount);
        if (static_cast<int64>(State.LostSecretBooks.Num()) + CopiesToDestroy > MAX_int32)
        {
            return Fail(Error, TEXT("The lost-book history exceeds the supported array range."));
        }
        for (int32 Copy = 0; Copy < CopiesToDestroy; ++Copy)
        {
            const FCopyAddress Chosen = Copies[State.Random.RandHelper(Copies.Num())];
            FBookRuntime& Runtime = State.Inventory.FindChecked(Chosen.BookId);
            Runtime.SecretCopies.RemoveAt(Chosen.CopyIndex);
            ShopEconomy::RefreshBookCounts(Runtime);
            State.LostSecretBooks.Add(Chosen.BookId);
            // Rebuild after removal so indices remain valid, including the last copy.
            if (Copy + 1 < CopiesToDestroy) Copies = SecretCopies(State, Catalog, Effect.TargetId);
        }
        return true;
    }

    bool AddCounter(int32& Counter, int32 Delta, FText& Error)
    {
        const int64 Next = static_cast<int64>(Counter) + Delta;
        if (Counter < 0 || Delta < 0 || Next > MAX_int32)
            return Fail(Error, TEXT("An effect counter is negative or exceeds the supported range."));
        Counter = static_cast<int32>(Next);
        return true;
    }

    bool ApplyBusinessPenalty(FShopRunState& State, const FRunRules& Rules, FText& Error)
    {
        if (Rules.PermanentRentPenalty < 0 || Rules.PermanentCustomerPenalty < 0
            || Rules.MaxRentPenalty < 0 || Rules.MaxCustomerPenalty < 0
            || State.RentPenalty < 0 || State.CustomerPenalty < 0)
            return Fail(Error, TEXT("Business penalty rules must be nonnegative."));
        const bool RentAvailable = Rules.PermanentRentPenalty > 0 && State.RentPenalty < Rules.MaxRentPenalty;
        const bool CustomerAvailable = Rules.PermanentCustomerPenalty > 0 && State.CustomerPenalty < Rules.MaxCustomerPenalty;
        if (!RentAvailable && !CustomerAvailable) return true;
        // 50/50 while both axes can still worsen; a capped axis yields to the other.
        const bool ChooseRent = RentAvailable && (!CustomerAvailable || State.Random.RandRange(0, 1) == 0);
        if (ChooseRent)
            State.RentPenalty = static_cast<int32>(FMath::Min<int64>(Rules.MaxRentPenalty,
                static_cast<int64>(State.RentPenalty) + Rules.PermanentRentPenalty));
        else
            State.CustomerPenalty = static_cast<int32>(FMath::Min<int64>(Rules.MaxCustomerPenalty,
                static_cast<int64>(State.CustomerPenalty) + Rules.PermanentCustomerPenalty));
        return true;
    }

    bool ReturnLostBook(FShopRunState& State, const FShopCatalog& Catalog,
        FName TargetId, FText& Error)
    {
        if (!ValidateSecretTarget(Catalog, TargetId, Error)) return false;
        TArray<FName> Pool;
        for (FName Id : State.LostSecretBooks)
        {
            const FBookData* Book = Catalog.Books.Find(Id);
            if (!Book || Book->Layer != EBookLayer::Inside)
                return Fail(Error, TEXT("Lost-book history references a missing or surface book."));
            if (TargetId.IsNone() || Id == TargetId) Pool.Add(Id);
        }
        if (Pool.IsEmpty())
        {
            if (Catalog.Rules.ReturnedBookFallbackPollution < 0)
                return Fail(Error, TEXT("Returned-book fallback pollution must be nonnegative."));
            return ShopEffects::ChangePollution(State, Catalog, Catalog.Rules.ReturnedBookFallbackPollution, Error);
        }
        Pool.Sort([](FName A, FName B) { return A.LexicalLess(B); });
        const FName Chosen = Pool[State.Random.RandHelper(Pool.Num())];
        FBookRuntime& Runtime = State.Inventory.FindOrAdd(Chosen);
        if (Runtime.SecretCopies.Num() == MAX_int32)
            return Fail(Error, TEXT("Returned-book inventory exceeds the supported array range."));
        FSecretBookCopy Returned;
        Returned.bSealed = false;
        Returned.bPolluted = true;
        Runtime.SecretCopies.Add(Returned);
        ShopEconomy::RefreshBookCounts(Runtime);
        State.LostSecretBooks.RemoveAt(State.LostSecretBooks.Find(Chosen));
        return true;
    }

    void ReduceCurrentNightQueue(FShopRunState& State, int32 Reduction)
    {
        const EGamePhase EffectivePhase = State.Phase == EGamePhase::Calm || State.Phase == EGamePhase::History
            ? State.ResumePhase : State.Phase;
        const bool bNightQueue = EffectivePhase == EGamePhase::NightShop || EffectivePhase == EGamePhase::NightSell ||
            (State.bNightCustomersGenerated && (EffectivePhase == EGamePhase::Inside || EffectivePhase == EGamePhase::Restock));
        if (!bNightQueue) return;
        int64 Remaining = -static_cast<int64>(Reduction);
        for (int32 Index = State.Customers.Num() - 1; Index >= 0 && Remaining > 0; --Index)
        {
            FCustomerRuntime& Customer = State.Customers[Index];
            if (!Customer.bServed && !Customer.bFake && Index != State.ActiveCustomer)
            {
                Customer.bServed = true;
                Customer.Resolution = EShopActionResult::Unavailable;
                --Remaining;
            }
        }
    }

    bool IsBlocked(const FShopRunState& State, FName Id)
    {
        for (const FDecreeRuntime& Runtime : State.Decrees)
        {
            if (Runtime.Id == Id && (Runtime.bActive || State.Turn < Runtime.CooldownUntilTurn))
            {
                return true;
            }
        }
        return false;
    }

    bool DecreeTiming(const FShopRunState& State, const FShopCatalog& Catalog,
        const FDecreeData& Data, int32& LoopholeAtTurn, int32& CooldownUntilTurn, FText& Error)
    {
        const int32 Delay = Data.LoopholeDelay == 0 ? Catalog.Rules.LoopholeDelayTurns : Data.LoopholeDelay;
        const int32 Cooldown = Data.CooldownTurns == 0 ? Catalog.Rules.DecreeCooldownTurns : Data.CooldownTurns;
        const int64 LoopholeTurn = static_cast<int64>(State.Turn) + Delay;
        const int64 CooldownTurn = LoopholeTurn + Cooldown;
        if (State.Turn < 0 || Delay <= 0 || Cooldown < 0
            || LoopholeTurn > MAX_int32 || CooldownTurn > MAX_int32)
        {
            return Fail(Error, TEXT("Invalid decree delay, cooldown, or logical turn range."));
        }
        LoopholeAtTurn = static_cast<int32>(LoopholeTurn);
        CooldownUntilTurn = static_cast<int32>(CooldownTurn);
        return true;
    }

    bool CanEnactOrdinary(const FShopRunState& State, const FShopCatalog& Catalog,
        FName Id, FText& Error)
    {
        const FDecreeData* Data = Catalog.Decrees.Find(Id);
        if (!Data || !Data->bEnabled || Id == EmergencyId)
            return Fail(Error, TEXT("The selected decree is unavailable."));
        const EPollutionStage Stage = ShopDecrees::GetStage(State.Pollution, Catalog.Rules);
        if (Stage < Data->MinStage || Stage > Data->MaxStage)
            return Fail(Error, TEXT("The selected decree is not available at this pollution stage."));
        if (IsBlocked(State, Id))
            return Fail(Error, TEXT("The selected decree is active or cooling down."));
        if (Data->PsychicCost < 0 || Data->PollutionCut < 0)
            return Fail(Error, TEXT("Decree resource cost and pollution reduction must be nonnegative."));
        if (State.Psychic < Data->PsychicCost)
            return Fail(Error, TEXT("There is not enough psychic energy to enact this decree."));
        int32 EndTurn = 0, CooldownEnd = 0;
        if (!DecreeTiming(State, Catalog, *Data, EndTurn, CooldownEnd, Error)) return false;
        // Trial includes the base cost before checking all additional costs together.
        // Its copied RNG cannot perturb the live run while choosing candidates.
        FShopRunState Trial = State;
        Trial.Psychic -= Data->PsychicCost;
        return ShopEffects::Apply(Trial, Catalog, Data->CostEffect, Id, EndTurn, true, Error);
    }

    bool TriggerLoophole(FShopRunState& State, const FShopCatalog& Catalog,
        int32 RuntimeIndex, FText& Error)
    {
        FDecreeRuntime& Runtime = State.Decrees[RuntimeIndex];
        if (!Runtime.bActive || Runtime.bLoopholeTriggered)
        {
            return true;
        }
        const FDecreeData* Data = Catalog.Decrees.Find(Runtime.Id);
        if (!Data)
        {
            return Fail(Error, TEXT("An active decree has no catalog row."));
        }
        const int32 Cooldown = Data->CooldownTurns == 0 ? Catalog.Rules.DecreeCooldownTurns : Data->CooldownTurns;
        const int64 CooldownEnd = static_cast<int64>(State.Turn) + Cooldown;
        if (Cooldown < 0 || CooldownEnd > MAX_int32)
        {
            return Fail(Error, TEXT("A decree cooldown exceeds the supported turn range."));
        }
        const FName Id = Runtime.Id;
        State.Modifiers.RemoveAll([Id](const FShopModifier& Modifier)
        {
            return Modifier.SourceId == Id && Modifier.EndTurn != INDEX_NONE;
        });
        Runtime.bActive = false;
        Runtime.bLoopholeTriggered = true;
        Runtime.CooldownUntilTurn = static_cast<int32>(CooldownEnd);
        // A loophole can deliberately leave a permanent penalty. Authored positive
        // durations override this default; permanent modifiers survive re-enactment.
        return ShopEffects::Apply(State, Catalog, Data->LoopholeEffect, Id, INDEX_NONE, false, Error);
    }
}

bool ShopEffects::ChangePollution(FShopRunState& State, const FShopCatalog& Catalog,
    int32 Delta, FText& Error)
{
    Error = FText::GetEmpty();
    if (Catalog.Rules.PollutionLimit <= 0 || State.Pollution < 0 || State.PendingPollutionBonus < 0)
        return Fail(Error, TEXT("Pollution, pending bonus, and limit configuration are invalid."));
    const int64 Bonus = Delta > 0 ? State.PendingPollutionBonus : 0;
    const int64 Next = static_cast<int64>(State.Pollution) + Delta + Bonus;
    if (Next > MAX_int32)
        return Fail(Error, TEXT("A pollution effect exceeds the supported integer range."));
    // Only scalar writes: callers may hold references to inventory or customer arrays.
    State.Pollution = static_cast<int32>(FMath::Max<int64>(0, Next));
    if (Delta > 0) State.PendingPollutionBonus = 0;
    if (Delta > 0) State.PeakPollutionThisCommand = FMath::Max(State.PeakPollutionThisCommand, State.Pollution);
    if (State.Pollution >= Catalog.Rules.PollutionLimit) State.bPollutionLimitReached = true;
    return true;
}

bool ShopEffects::Apply(FShopRunState& State, const FShopCatalog& Catalog,
    const TArray<FShopEffect>& Effects, FName SourceId, int32 DefaultEndTurn,
    bool bIsCost, FText& Error)
{
    Error = FText::GetEmpty();
    for (const auto& Pair : State.Inventory)
    {
        const FBookData* Book = Catalog.Books.Find(Pair.Key);
        if (Book && Book->Layer == EBookLayer::Inside && !ShopEconomy::IsSecretInventoryValid(Pair.Value))
            return Fail(Error, TEXT("Secret-copy inventory is inconsistent; effects cannot repair or create owned copies implicitly."));
    }
    for (const FShopEffect& Effect : Effects)
    {
        if (IsPassive(Effect.Type))
        {
            if (IsMultiplier(Effect.Type)
                && (!FMath::IsFinite(Effect.Multiplier) || Effect.Multiplier < 0.f))
            {
                return Fail(Error, TEXT("An effect multiplier must be finite and nonnegative."));
            }
            FShopModifier Modifier;
            if (!CalculateEndTurn(State, Effect, DefaultEndTurn, Modifier.EndTurn, Error))
            {
                return false;
            }
            Modifier.SourceId = SourceId;
            Modifier.Type = Effect.Type;
            Modifier.Amount = Effect.Amount;
            Modifier.Multiplier = Effect.Multiplier;
            const bool NightScoped = Effect.Type == EShopEffectType::NightIncomeMultiplier
                || Effect.Type == EShopEffectType::CustomerCountDelta;
            if (NightScoped) Modifier.EndTurn = INDEX_NONE; // Settled/removed by the night coordinator.
            int32 PreviousCustomerDelta = 0;
            for (const FShopModifier& Previous : State.Modifiers)
                if (Previous.SourceId == SourceId && Previous.Type == Effect.Type)
                    PreviousCustomerDelta = Previous.Amount;
            // In particular, Watch's permanent fee and pollution each have one entry
            // per source, even when the decree has completed several life cycles.
            State.Modifiers.RemoveAll([SourceId, &Effect](const FShopModifier& Previous)
            {
                return Previous.SourceId == SourceId && Previous.Type == Effect.Type;
            });
            State.Modifiers.Add(Modifier);
            if (Effect.Type == EShopEffectType::CustomerCountDelta)
            {
                const int64 ExtraReduction = static_cast<int64>(Effect.Amount) - PreviousCustomerDelta;
                if (ExtraReduction < 0)
                    ReduceCurrentNightQueue(State, static_cast<int32>(FMath::Max<int64>(MIN_int32, ExtraReduction)));
            }
            continue;
        }

        switch (Effect.Type)
        {
        case EShopEffectType::Money:
            if (!ShopEconomy::AddMoney(State, Effect.Amount, Error, bIsCost))
            {
                return false;
            }
            break;
        case EShopEffectType::Psychic:
            if (Catalog.Rules.PsychicMax < 0)
                return Fail(Error, TEXT("PsychicMax must be nonnegative."));
            if (bIsCost && Effect.Amount < 0 && static_cast<int64>(State.Psychic) + Effect.Amount < 0)
                return Fail(Error, TEXT("There is not enough psychic energy to pay this effect."));
            {
                const int64 NextPsychic = static_cast<int64>(State.Psychic) + Effect.Amount;
                State.Psychic = static_cast<int32>(FMath::Clamp<int64>(NextPsychic, 0, Catalog.Rules.PsychicMax));
            }
            break;
        case EShopEffectType::Enlighten:
            if (!ChangeNonnegativeResource(State.Enlighten, Effect.Amount, bIsCost, Error))
            {
                return false;
            }
            break;
        case EShopEffectType::Pollution:
            if (bIsCost && Effect.Amount < 0 && static_cast<int64>(State.Pollution) + Effect.Amount < 0)
                return Fail(Error, TEXT("There is not enough pollution to pay this effect."));
            if (!ChangePollution(State, Catalog, Effect.Amount, Error)) return false;
            break;
        case EShopEffectType::AddClue:
            if (Effect.TargetId.IsNone())
            {
                return Fail(Error, TEXT("AddClue requires a nonempty TargetId."));
            }
            State.Clues.AddUnique(Effect.TargetId);
            break;
        case EShopEffectType::RemoveClue:
        {
            FName Clue = Effect.TargetId;
            if (Clue.IsNone() && State.Clues.Num() > 0)
            {
                TArray<FName> Sorted = State.Clues;
                Sorted.Sort([](FName A, FName B) { return A.LexicalLess(B); });
                Clue = Sorted[State.Random.RandHelper(Sorted.Num())];
            }
            if (Clue.IsNone() || !State.Clues.Contains(Clue))
            {
                if (bIsCost)
                {
                    return Fail(Error, TEXT("The required history clue is unavailable."));
                }
                break;
            }
            State.Clues.Remove(Clue);
            break;
        }
        case EShopEffectType::DestroySecretBooks:
            if (!DestroySecretCopies(State, Catalog, Effect, bIsCost, Error))
            {
                return false;
            }
            break;
        case EShopEffectType::AlterSecretBook:
        {
            if (!ValidateSecretTarget(Catalog, Effect.TargetId, Error))
            {
                return false;
            }
            const TArray<FCopyAddress> Copies = SecretCopies(State, Catalog, Effect.TargetId);
            if (Copies.Num() == 0)
            {
                if (bIsCost)
                {
                    return Fail(Error, TEXT("There is no owned secret book to alter."));
                }
                break;
            }
            const FCopyAddress Chosen = Copies[State.Random.RandHelper(Copies.Num())];
            FBookRuntime& Runtime = State.Inventory.FindChecked(Chosen.BookId);
            Runtime.SecretCopies[Chosen.CopyIndex].bAltered = true;
            ShopEconomy::RefreshBookCounts(Runtime);
            break;
        }
        case EShopEffectType::NextPollutionBonus:
            if (!AddCounter(State.PendingPollutionBonus, Effect.Amount, Error)) return false;
            break;
        case EShopEffectType::SpawnFakeCustomer:
            if (!AddCounter(State.PendingFakeCustomers, Effect.Amount, Error)) return false;
            break;
        case EShopEffectType::SkipNightDecay:
            if (!AddCounter(State.SkipDecayNights, Effect.Amount, Error)) return false;
            break;
        case EShopEffectType::UnlockSecretBook:
        {
            if (Effect.Amount < 0 || !ValidateSecretTarget(Catalog, Effect.TargetId, Error))
                return Fail(Error, TEXT("UnlockSecretBook requires a valid target and nonnegative copy count."));
            // The shelf loophole releases an altered, still-sealed copy. It must
            // not substitute an unrelated sealed book after its target was lost.
            TArray<FCopyAddress> Copies = SecretCopies(State, Catalog, Effect.TargetId, true, true);
            const int32 Count = FMath::Min(Effect.Amount, Copies.Num());
            for (int32 Index = 0; Index < Count; ++Index)
            {
                const FCopyAddress Chosen = Copies[State.Random.RandHelper(Copies.Num())];
                FBookRuntime& Runtime = State.Inventory.FindChecked(Chosen.BookId);
                Runtime.SecretCopies[Chosen.CopyIndex].bSealed = false;
                ShopEconomy::RefreshBookCounts(Runtime);
                if (Index + 1 < Count) Copies = SecretCopies(State, Catalog, Effect.TargetId, true, true);
            }
            // Pollution backlash is a separate authored Pollution effect. It still
            // executes when no altered sealed copy remains, and consumes a bonus only once.
            break;
        }
        case EShopEffectType::PermanentBusinessPenalty:
            if (!ApplyBusinessPenalty(State, Catalog.Rules, Error)) return false;
            break;
        case EShopEffectType::ReturnLostSecretBook:
            if (!ReturnLostBook(State, Catalog, Effect.TargetId, Error)) return false;
            break;
        case EShopEffectType::ResetPollutionThresholds:
            State.StageResetPending = true;
            break;
        default:
            return Fail(Error, TEXT("The effect type is not supported."));
        }
    }
    return true;
}

float ShopEffects::Multiplier(const FShopRunState& State, EShopEffectType Type)
{
    double Result = 1.0;
    for (const FShopModifier& Modifier : State.Modifiers)
    {
        if (Modifier.Type == Type && IsLiveModifier(Modifier, State.Turn))
        {
            if (!FMath::IsFinite(Modifier.Multiplier) || Modifier.Multiplier < 0.f)
            {
                continue;
            }
            Result = FMath::Min(Result * Modifier.Multiplier, static_cast<double>(TNumericLimits<float>::Max()));
        }
    }
    return static_cast<float>(Result);
}

int32 ShopEffects::Sum(const FShopRunState& State, EShopEffectType Type)
{
    int64 Result = 0;
    for (const FShopModifier& Modifier : State.Modifiers)
    {
        if (Modifier.Type == Type && IsLiveModifier(Modifier, State.Turn))
        {
            Result += Modifier.Amount;
        }
    }
    return static_cast<int32>(FMath::Clamp<int64>(Result, MIN_int32, MAX_int32));
}

FDecreeData ShopDecrees::GetFallbackDecree(const FRunRules& Rules)
{
    FDecreeData Data;
    Data.Id = EmergencyId;
    Data.DisplayName = FText::FromString(TEXT("应急镇定"));
    Data.Quality = EDecreeQuality::Bronze;
    Data.PsychicCost = 0;
    Data.PollutionCut = Rules.EmergencyPollutionCut;
    Data.MinStage = EPollutionStage::Light;
    Data.MaxStage = EPollutionStage::Heavy;
    Data.bFallback = true;
    Data.bEnabled = true;
    Data.CooldownTurns = 0;
    Data.EffectText = FText::Format(FText::FromString(TEXT("污染降低 {0}。")), FText::AsNumber(Rules.EmergencyPollutionCut));
    Data.CostText = FText::Format(FText::FromString(TEXT("支付 {0} 金钱，可负债；不消耗灵能。")), FText::AsNumber(Rules.EmergencyMoneyCost));
    Data.LoopholeText = FText::FromString(TEXT("不产生持续律令、冷却或漏洞。"));
    Data.Text = FText::FromString(TEXT("当前没有可支付的普通律令时提供的应急处置。"));
    FShopEffect Fee;
    Fee.Type = EShopEffectType::Money;
    Fee.Amount = -FMath::Max(0, Rules.EmergencyMoneyCost);
    Data.CostEffect.Add(Fee);
    return Data;
}

EPollutionStage ShopDecrees::GetStage(int32 Pollution, const FRunRules& Rules)
{
    if (Pollution >= Rules.HeavyThreshold) return EPollutionStage::Heavy;
    if (Pollution >= Rules.MediumThreshold) return EPollutionStage::Medium;
    if (Pollution >= Rules.LightThreshold) return EPollutionStage::Light;
    return EPollutionStage::Safe;
}

void ShopDecrees::DrawCandidates(FShopRunState& State, const FShopCatalog& Catalog)
{
    State.DecreeCandidates.Reset();
    for (const TPair<FName, FDecreeData>& Pair : Catalog.Decrees)
    {
        FText Error;
        if (CanEnactOrdinary(State, Catalog, Pair.Key, Error))
        {
            State.DecreeCandidates.Add(Pair.Key);
        }
    }
    State.DecreeCandidates.Sort([](FName A, FName B) { return A.LexicalLess(B); });
    for (int32 Index = State.DecreeCandidates.Num() - 1; Index > 0; --Index)
    {
        State.DecreeCandidates.Swap(Index, State.Random.RandRange(0, Index));
    }
    const EPollutionStage Stage = GetStage(State.Pollution, Catalog.Rules);
    const int32 ConfiguredCount = Stage == EPollutionStage::Heavy ? Catalog.Rules.DecreeCandidateCountHeavy
        : Stage == EPollutionStage::Medium ? Catalog.Rules.DecreeCandidateCountMedium
        : Catalog.Rules.DecreeCandidateCountLight;
    // Validation rejects nonpositive configuration; retaining at least one available
    // card also avoids a dead-end when this pure helper is used directly.
    if (State.DecreeCandidates.IsEmpty()) State.DecreeCandidates.Add(EmergencyId);
    else State.DecreeCandidates.SetNum(FMath::Clamp(ConfiguredCount, 1, State.DecreeCandidates.Num()));
}

bool ShopDecrees::CanEnact(const FShopRunState& State, const FShopCatalog& Catalog,
    FName Id, FText& Error)
{
    Error = FText::GetEmpty();
    if (!State.DecreeCandidates.Contains(Id))
    {
        return Fail(Error, TEXT("The selected decree is not in the current calm candidate list."));
    }
    if (Id == EmergencyId)
    {
        if (Catalog.Rules.EmergencyPollutionCut < 0 || Catalog.Rules.EmergencyMoneyCost < 0)
            return Fail(Error, TEXT("Emergency decree cost and pollution reduction must be nonnegative."));
        FShopRunState Trial = State;
        return ShopEconomy::AddMoney(Trial, -Catalog.Rules.EmergencyMoneyCost, Error, false)
            && ShopEffects::ChangePollution(Trial, Catalog, -Catalog.Rules.EmergencyPollutionCut, Error);
    }
    return CanEnactOrdinary(State, Catalog, Id, Error);
}

bool ShopDecrees::Enact(FShopRunState& State, const FShopCatalog& Catalog,
    FName Id, FText& Error)
{
    if (!CanEnact(State, Catalog, Id, Error))
    {
        return false;
    }
    if (Id == EmergencyId)
    {
        // This is a transaction-safe one-shot, not an eighth authored decree.
        return ShopEconomy::AddMoney(State, -Catalog.Rules.EmergencyMoneyCost, Error, false)
            && ShopEffects::ChangePollution(State, Catalog, -Catalog.Rules.EmergencyPollutionCut, Error);
    }
    const FDecreeData& Data = Catalog.Decrees.FindChecked(Id);
    FDecreeRuntime Runtime;
    Runtime.Id = Id;
    Runtime.EnactedTurn = State.Turn;
    if (!DecreeTiming(State, Catalog, Data, Runtime.LoopholeAtTurn, Runtime.CooldownUntilTurn, Error))
    {
        return false;
    }
    State.Psychic -= Data.PsychicCost;
    if (!ShopEffects::Apply(State, Catalog, Data.CostEffect, Id, Runtime.LoopholeAtTurn, true, Error))
    {
        return false;
    }
    FShopEffect Reduction;
    Reduction.Type = EShopEffectType::Pollution;
    Reduction.Amount = -Data.PollutionCut;
    if (!ShopEffects::Apply(State, Catalog, { Reduction }, Id, Runtime.LoopholeAtTurn, false, Error)
        || !ShopEffects::Apply(State, Catalog, Data.Effects, Id, Runtime.LoopholeAtTurn, false, Error))
    {
        return false;
    }
    State.Decrees.RemoveAll([Id](const FDecreeRuntime& Existing) { return Existing.Id == Id; });
    State.Decrees.Add(Runtime);
    if (Data.Quality == EDecreeQuality::Gold)
    {
        State.bGoldDuringHeavyGrace = true;
    }
    return true;
}

bool ShopDecrees::TickTurn(FShopRunState& State, const FShopCatalog& Catalog,
    TArray<FName>& Triggered, FText& Error)
{
    Error = FText::GetEmpty();
    Triggered.Reset();
    if (State.Turn < 0 || State.Turn == MAX_int32)
    {
        return Fail(Error, TEXT("The logical turn cannot be advanced safely."));
    }
    ++State.Turn;
    TArray<FName> ThisTurn;
    for (int32 Index = 0; Index < State.Decrees.Num(); ++Index)
    {
        const FDecreeRuntime& Runtime = State.Decrees[Index];
        if (Runtime.bActive && !Runtime.bLoopholeTriggered && Runtime.LoopholeAtTurn <= State.Turn)
        {
            const FName Id = Runtime.Id;
            if (!TriggerLoophole(State, Catalog, Index, Error))
            {
                return false;
            }
            ThisTurn.Add(Id);
        }
    }
    State.Modifiers.RemoveAll([&State](const FShopModifier& Modifier)
    {
        return Modifier.EndTurn != INDEX_NONE && Modifier.EndTurn <= State.Turn;
    });
    Triggered = MoveTemp(ThisTurn);
    return true;
}

bool ShopDecrees::TriggerStageLoophole(FShopRunState& State,
    const FShopCatalog& Catalog, FText& Error)
{
    TArray<FName> Triggered;
    return TickTurn(State, Catalog, Triggered, Error);
}
