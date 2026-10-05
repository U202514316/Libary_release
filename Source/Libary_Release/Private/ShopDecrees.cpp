#include "ShopDecrees.h"

#include "ShopEconomy.h"

namespace
{
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
            return true;
        default:
            return false;
        }
    }

    bool IsMultiplier(EShopEffectType Type)
    {
        return Type == EShopEffectType::IncomeMultiplier
            || Type == EShopEffectType::PsychicGainMultiplier
            || Type == EShopEffectType::DecayMultiplier;
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

    TArray<FName> OwnedSecretIds(const FShopRunState& State, const FShopCatalog& Catalog,
        FName TargetId)
    {
        TArray<FName> Result;
        for (const TPair<FName, FBookRuntime>& Pair : State.Inventory)
        {
            const FBookData* Book = Catalog.Books.Find(Pair.Key);
            if (Book && Book->Layer == EBookLayer::Inside && Pair.Value.Stock > 0
                && (TargetId.IsNone() || Pair.Key == TargetId))
            {
                Result.Add(Pair.Key);
            }
        }
        Result.Sort([](FName A, FName B) { return A.LexicalLess(B); });
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
        TArray<FName> Ids = OwnedSecretIds(State, Catalog, Effect.TargetId);
        int64 TotalCopies = 0;
        for (FName Id : Ids)
        {
            TotalCopies += State.Inventory.FindChecked(Id).Stock;
        }
        if (bIsCost && TotalCopies < Effect.Amount)
        {
            return Fail(Error, TEXT("There are not enough owned secret books to pay this cost."));
        }
        const int32 CopiesToDestroy = static_cast<int32>(FMath::Min<int64>(TotalCopies, Effect.Amount));
        for (int32 Copy = 0; Copy < CopiesToDestroy; ++Copy)
        {
            // Weight by copies, not by distinct titles. The sorted order makes seeded
            // selection independent of TMap iteration order.
            int64 Pick = TotalCopies <= MAX_int32
                ? State.Random.RandHelper(static_cast<int32>(TotalCopies))
                : static_cast<int64>(State.Random.GetFraction() * static_cast<double>(TotalCopies));
            Pick = FMath::Min<int64>(Pick, TotalCopies - 1);
            for (int32 Index = 0; Index < Ids.Num(); ++Index)
            {
                FBookRuntime& Runtime = State.Inventory.FindChecked(Ids[Index]);
                if (Pick >= Runtime.Stock)
                {
                    Pick -= Runtime.Stock;
                    continue;
                }
                // Aggregate inventory has no per-copy identity. Clamp the read count
                // to the surviving stock, matching the stock/read-count invariant.
                --Runtime.Stock;
                Runtime.ReadCopies = FMath::Clamp(Runtime.ReadCopies, 0, Runtime.Stock);
                if (Runtime.Stock == 0)
                {
                    Ids.RemoveAt(Index);
                }
                --TotalCopies;
                break;
            }
        }
        return true;
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
        const int64 LoopholeTurn = static_cast<int64>(State.Turn) + Delay;
        const int64 CooldownTurn = LoopholeTurn + Data.CooldownTurns;
        if (State.Turn < 0 || Delay <= 0 || Data.CooldownTurns < 0
            || LoopholeTurn > MAX_int32 || CooldownTurn > MAX_int32)
        {
            return Fail(Error, TEXT("Invalid decree delay, cooldown, or logical turn range."));
        }
        LoopholeAtTurn = static_cast<int32>(LoopholeTurn);
        CooldownUntilTurn = static_cast<int32>(CooldownTurn);
        return true;
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
        const int64 CooldownEnd = static_cast<int64>(State.Turn) + Data->CooldownTurns;
        if (Data->CooldownTurns < 0 || CooldownEnd > MAX_int32)
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

bool ShopEffects::Apply(FShopRunState& State, const FShopCatalog& Catalog,
    const TArray<FShopEffect>& Effects, FName SourceId, int32 DefaultEndTurn,
    bool bIsCost, FText& Error)
{
    Error = FText::GetEmpty();
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
            State.Modifiers.Add(Modifier);
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
            if (!ChangeNonnegativeResource(State.Psychic, Effect.Amount, bIsCost, Error))
            {
                return false;
            }
            break;
        case EShopEffectType::Enlighten:
            if (!ChangeNonnegativeResource(State.Enlighten, Effect.Amount, bIsCost, Error))
            {
                return false;
            }
            break;
        case EShopEffectType::Pollution:
            if (Catalog.Rules.PollutionLimit <= 0)
            {
                return Fail(Error, TEXT("PollutionLimit must be positive."));
            }
            if (!ChangeNonnegativeResource(State.Pollution, Effect.Amount, bIsCost, Error))
            {
                return false;
            }
            if (State.Pollution >= Catalog.Rules.PollutionLimit)
            {
                State.bPollutionLimitReached = true;
            }
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
            const TArray<FName> Ids = OwnedSecretIds(State, Catalog, Effect.TargetId);
            if (Ids.Num() == 0)
            {
                if (bIsCost)
                {
                    return Fail(Error, TEXT("There is no owned secret book to alter."));
                }
                break;
            }
            const FName Id = Ids[State.Random.RandHelper(Ids.Num())];
            // No mechanical consequence beyond this flag is invented for sample data.
            State.Inventory.FindChecked(Id).bAltered = true;
            break;
        }
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
        if (Pair.Value.bEnabled && !IsBlocked(State, Pair.Key))
        {
            State.DecreeCandidates.Add(Pair.Key);
        }
    }
    State.DecreeCandidates.Sort([](FName A, FName B) { return A.LexicalLess(B); });
    for (int32 Index = State.DecreeCandidates.Num() - 1; Index > 0; --Index)
    {
        State.DecreeCandidates.Swap(Index, State.Random.RandRange(0, Index));
    }
    State.DecreeCandidates.SetNum(FMath::Clamp(Catalog.Rules.DecreeCandidateCount, 0, State.DecreeCandidates.Num()));
}

bool ShopDecrees::CanEnact(const FShopRunState& State, const FShopCatalog& Catalog,
    FName Id, FText& Error)
{
    Error = FText::GetEmpty();
    const FDecreeData* Data = Catalog.Decrees.Find(Id);
    if (!Data || !Data->bEnabled)
    {
        return Fail(Error, TEXT("The selected decree is unavailable."));
    }
    if (!State.DecreeCandidates.Contains(Id))
    {
        return Fail(Error, TEXT("The selected decree is not in today's candidate list."));
    }
    if (IsBlocked(State, Id))
    {
        return Fail(Error, TEXT("The selected decree is active or cooling down."));
    }
    if (Data->PsychicCost < 0 || Data->PollutionCut < 0)
    {
        return Fail(Error, TEXT("Decree resource cost and pollution reduction must be nonnegative."));
    }
    if (State.Psychic < Data->PsychicCost)
    {
        return Fail(Error, TEXT("There is not enough psychic energy to enact this decree."));
    }
    int32 EndTurn = 0, CooldownUntilTurn = 0;
    if (!DecreeTiming(State, Catalog, *Data, EndTurn, CooldownUntilTurn, Error))
    {
        return false;
    }
    // Check compound costs against one speculative state, so two individually
    // affordable costs cannot spend the same resource twice. RNG is copied as well.
    FShopRunState Trial = State;
    Trial.Psychic -= Data->PsychicCost;
    return ShopEffects::Apply(Trial, Catalog, Data->CostEffect, Id, EndTurn, true, Error);
}

bool ShopDecrees::Enact(FShopRunState& State, const FShopCatalog& Catalog,
    FName Id, FText& Error)
{
    if (!CanEnact(State, Catalog, Id, Error))
    {
        return false;
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
    Error = FText::GetEmpty();
    if (!Catalog.Rules.bStageCrossTriggersLoophole)
    {
        return true;
    }
    TArray<int32> Eligible;
    for (int32 Index = 0; Index < State.Decrees.Num(); ++Index)
    {
        if (State.Decrees[Index].bActive && !State.Decrees[Index].bLoopholeTriggered)
        {
            Eligible.Add(Index);
        }
    }
    Eligible.Sort([&State](int32 A, int32 B)
    {
        return State.Decrees[A].Id.LexicalLess(State.Decrees[B].Id);
    });
    return Eligible.Num() == 0
        || TriggerLoophole(State, Catalog, Eligible[State.Random.RandHelper(Eligible.Num())], Error);
}
