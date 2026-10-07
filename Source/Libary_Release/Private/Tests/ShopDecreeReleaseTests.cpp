#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopDecrees.h"
#include "ShopEconomy.h"

namespace ShopDecreeReleaseTests
{
    FShopEffect Effect(EShopEffectType Type, int32 Amount = 0, int32 Duration = 0)
    {
        FShopEffect Result;
        Result.Type = Type;
        Result.Amount = Amount;
        Result.DurationTurns = Duration;
        return Result;
    }

    FDecreeData Rule(FName Id, int32 Cost = 8)
    {
        FDecreeData Result;
        Result.Id = Id;
        Result.PsychicCost = Cost;
        Result.PollutionCut = 0;
        Result.LoopholeDelay = 2;
        Result.CooldownTurns = 0; // Exercise the global cooldown fallback.
        return Result;
    }

    void AddSecret(FShopCatalog& Catalog, FShopRunState& State, FName Id, int32 Count)
    {
        FBookData Book;
        Book.BookType = EBookType::Secret;
        Book.Layer = EBookLayer::Inside;
        Catalog.Books.Add(Id, Book);
        FBookRuntime Runtime;
        Runtime.SecretCopies.SetNum(Count);
        ShopEconomy::RefreshBookCounts(Runtime);
        State.Inventory.Add(Id, Runtime);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreePollutionReleaseTest,
    "Bookstore.ProgramA.ReleaseDecrees.NextPollutionAndUnlimitedPsychic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreePollutionReleaseTest::RunTest(const FString& Parameters)
{
    using namespace ShopDecreeReleaseTests;
    FShopCatalog Catalog;
    FShopRunState State;
    FText Error;
    State.Pollution = 20;
    TestTrue(TEXT("Queue next-pollution bonus"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::NextPollutionBonus, 5) }, TEXT("quiet"), -1, false, Error));
    TestTrue(TEXT("Reduction succeeds"), ShopEffects::ChangePollution(State, Catalog, -3, Error));
    TestTrue(TEXT("Zero change succeeds"), ShopEffects::ChangePollution(State, Catalog, 0, Error));
    TestEqual(TEXT("Reduction and zero preserve the bonus"), State.PendingPollutionBonus, 5);
    TestTrue(TEXT("Next positive change succeeds"), ShopEffects::ChangePollution(State, Catalog, 2, Error));
    TestEqual(TEXT("Only the positive change includes bonus"), State.Pollution, 24);
    TestEqual(TEXT("Bonus is consumed"), State.PendingPollutionBonus, 0);
    TestEqual(TEXT("Command peak is recorded"), State.PeakPollutionThisCommand, 24);
    TestTrue(TEXT("Following change succeeds"), ShopEffects::ChangePollution(State, Catalog, 2, Error));
    TestEqual(TEXT("Bonus is not repeated"), State.Pollution, 26);

    State.Psychic = 99;
    TestTrue(TEXT("Psychic reward applies"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::Psychic, 8) }, TEXT("reward"), -1, false, Error));
    TestEqual(TEXT("Psychic reward crosses the former cap"), State.Psychic, 107);
    Catalog.Rules.PsychicMax = 100; // Old saved tables must not restore a gameplay cap.
    TestTrue(TEXT("Psychic cost works above the former cap"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::Psychic, -8) }, TEXT("cost"), -1, true, Error));
    TestEqual(TEXT("Only the cost is deducted"), State.Psychic, 99);
    TestFalse(TEXT("Insufficient psychic cost still rejects"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::Psychic, -100) }, TEXT("cost"), -1, true, Error));
    TestEqual(TEXT("Rejected cost preserves psychic"), State.Psychic, 99);
    State.Psychic = MAX_int32 - 3;
    TestFalse(TEXT("Psychic integer overflow rejects instead of wrapping"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::Psychic, 8) }, TEXT("reward"), -1, false, Error));
    TestEqual(TEXT("Overflow preserves psychic"), State.Psychic, MAX_int32 - 3);
    State.Pollution = 99;
    TestTrue(TEXT("Pollution reaches limit"), ShopEffects::ChangePollution(State, Catalog, 1, Error));
    TestTrue(TEXT("Subsequent reduction applies"), ShopEffects::ChangePollution(State, Catalog, -50, Error));
    TestTrue(TEXT("Terminal limit remains sticky"), State.bPollutionLimitReached);
    TestEqual(TEXT("Peak survives reduction"), State.PeakPollutionThisCommand, 100);

    State.Pollution = MAX_int32 - 1;
    State.PendingPollutionBonus = 5;
    TestFalse(TEXT("Overflow is rejected"), ShopEffects::ChangePollution(State, Catalog, 2, Error));
    TestEqual(TEXT("Overflow preserves pollution"), State.Pollution, MAX_int32 - 1);
    TestEqual(TEXT("Overflow does not consume bonus"), State.PendingPollutionBonus, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreeCandidateReleaseTest,
    "Bookstore.ProgramA.ReleaseDecrees.CandidatesFallbackAndLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreeCandidateReleaseTest::RunTest(const FString& Parameters)
{
    using namespace ShopDecreeReleaseTests;
    const FName Copper(TEXT("TEST_copper")), Silver(TEXT("TEST_silver")), Gold(TEXT("TEST_gold"));
    FShopCatalog Catalog;
    Catalog.Decrees.Add(Copper, Rule(Copper));
    FDecreeData SilverRow = Rule(Silver, 18);
    SilverRow.MinStage = EPollutionStage::Medium;
    Catalog.Decrees.Add(Silver, SilverRow);
    FDecreeData GoldRow = Rule(Gold, 35);
    GoldRow.MinStage = EPollutionStage::Heavy;
    GoldRow.CostEffect.Add(Effect(EShopEffectType::RemoveClue));
    Catalog.Decrees.Add(Gold, GoldRow);

    FShopRunState State;
    State.Random.Initialize(1234);
    State.Pollution = 31;
    State.Money = 5;
    FText Error;
    ShopDecrees::DrawCandidates(State, Catalog);
    TestEqual(TEXT("No affordable normal rule produces one fallback"), State.DecreeCandidates.Num(), 1);
    const FDecreeData Fallback = ShopDecrees::GetFallbackDecree(Catalog.Rules);
    TestTrue(TEXT("Fallback is advertised"), State.DecreeCandidates.Contains(Fallback.Id));
    TestTrue(TEXT("Fallback may create debt"), ShopDecrees::Enact(State, Catalog, Fallback.Id, Error));
    TestEqual(TEXT("Fallback charges the configured fee"), State.Money, 5 - Catalog.Rules.EmergencyMoneyCost);
    TestEqual(TEXT("Fallback reduces pollution"), State.Pollution, 31 - Catalog.Rules.EmergencyPollutionCut);
    TestEqual(TEXT("Fallback creates no active record"), State.Decrees.Num(), 0);
    TestEqual(TEXT("Fallback does not advance time"), State.Turn, 0);

    State.Pollution = 31;
    State.Psychic = 100;
    ShopDecrees::DrawCandidates(State, Catalog);
    TestEqual(TEXT("Light-stage filter leaves copper"), State.DecreeCandidates.Num(), 1);
    TestTrue(TEXT("Copper is selectable"), State.DecreeCandidates.Contains(Copper));
    State.Pollution = 90;
    ShopDecrees::DrawCandidates(State, Catalog);
    TestFalse(TEXT("Gold cannot waive its clue cost"), State.DecreeCandidates.Contains(Gold));
    State.Clues.Add(TEXT("TEST_clue"));
    ShopDecrees::DrawCandidates(State, Catalog);
    TestTrue(TEXT("Gold becomes affordable with its clue"), State.DecreeCandidates.Contains(Gold));

    Catalog.Decrees[Copper].LoopholeEffect.Add(Effect(EShopEffectType::NextPollutionBonus, 5));
    TestTrue(TEXT("Copper enactment succeeds"), ShopDecrees::Enact(State, Catalog, Copper, Error));
    TestEqual(TEXT("A new decree keeps its full TTL at enactment"), State.Turn, 0);
    TestFalse(TEXT("Active decree cannot be enacted twice"), ShopDecrees::CanEnact(State, Catalog, Copper, Error));
    TestTrue(TEXT("Stage settlement advances one turn"), ShopDecrees::TriggerStageLoophole(State, Catalog, Error));
    TestEqual(TEXT("First settlement has no backlash"), State.PendingPollutionBonus, 0);
    TArray<FName> Triggered;
    TestTrue(TEXT("Second settlement succeeds"), ShopDecrees::TickTurn(State, Catalog, Triggered, Error));
    TestTrue(TEXT("Second settlement triggers copper"), Triggered.Contains(Copper));
    TestEqual(TEXT("Backlash is queued, not applied to current pollution"), State.PendingPollutionBonus, 5);
    TestEqual(TEXT("Cooldown starts after expiry"), State.Decrees[0].CooldownUntilTurn, 5);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        TestTrue(TEXT("Cooldown settlement advances"), ShopDecrees::TickTurn(State, Catalog, Triggered, Error));
        ShopDecrees::DrawCandidates(State, Catalog);
        TestFalse(TEXT("Rule remains unavailable before third cooldown settlement"), State.DecreeCandidates.Contains(Copper));
        TestEqual(TEXT("Expired backlash never fires twice"), Triggered.Num(), 0);
    }
    TestTrue(TEXT("Third cooldown settlement advances"), ShopDecrees::TickTurn(State, Catalog, Triggered, Error));
    ShopDecrees::DrawCandidates(State, Catalog);
    TestTrue(TEXT("Rule returns after three complete cooldown settlements"), State.DecreeCandidates.Contains(Copper));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreeCopiesReleaseTest,
    "Bookstore.ProgramA.ReleaseDecrees.SecretCopyCostsAndReturn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreeCopiesReleaseTest::RunTest(const FString& Parameters)
{
    using namespace ShopDecreeReleaseTests;
    const FName BookId(TEXT("TEST_secret"));
    FShopCatalog Catalog;
    FShopRunState State;
    State.Random.Initialize(4321);
    AddSecret(Catalog, State, BookId, 3);
    FText Error;
    TestTrue(TEXT("Alteration targets one owned copy"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::AlterSecretBook) }, TEXT("shelf"), 2, true, Error));
    int32 Altered = 0;
    for (const FSecretBookCopy& Copy : State.Inventory[BookId].SecretCopies) if (Copy.bAltered) ++Altered;
    TestEqual(TEXT("Only one copy is altered"), Altered, 1);
    State.PendingPollutionBonus = 5;
    TestTrue(TEXT("Unlock is independent from the following pollution effect"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::UnlockSecretBook, 1), Effect(EShopEffectType::Pollution, 10) }, TEXT("shelf"), -1, false, Error));
    int32 Unsealed = 0;
    for (const FSecretBookCopy& Copy : State.Inventory[BookId].SecretCopies)
    {
        if (!Copy.bSealed) ++Unsealed;
        TestEqual(TEXT("Unlock only releases the altered copy"), Copy.bSealed, !Copy.bAltered);
    }
    TestEqual(TEXT("Exactly one sealed copy is unlocked"), Unsealed, 1);
    TestEqual(TEXT("Independent backlash consumes the bonus once"), State.Pollution, 15);

    TestTrue(TEXT("Two-copy cost succeeds"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::DestroySecretBooks, 2) }, TEXT("tide"), 2, true, Error));
    TestEqual(TEXT("One owned copy survives"), State.Inventory[BookId].Stock, 1);
    TestEqual(TEXT("Each destroyed copy enters history"), State.LostSecretBooks.Num(), 2);
    TestFalse(TEXT("Insufficient two-copy cost is rejected"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::DestroySecretBooks, 2) }, TEXT("tide"), 2, true, Error));
    TestEqual(TEXT("Rejected cost keeps the last copy"), State.Inventory[BookId].Stock, 1);
    TestTrue(TEXT("Destroying the final copy succeeds"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::DestroySecretBooks, 1) }, TEXT("tide"), 2, true, Error));
    TestEqual(TEXT("Deleting the last copy does not resurrect aggregate stock"), State.Inventory[BookId].Stock, 0);
    TestTrue(TEXT("A lost copy returns"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::ReturnLostSecretBook, 1) }, TEXT("tide"), -1, false, Error));
    TestEqual(TEXT("Exactly one copy returns"), State.Inventory[BookId].Stock, 1);
    TestEqual(TEXT("Returned copy is consumed from the lost pool"), State.LostSecretBooks.Num(), 2);
    TestTrue(TEXT("Returned copy is polluted"), State.Inventory[BookId].SecretCopies[0].bPolluted);
    State.LostSecretBooks.Reset();
    const int32 PollutionBefore = State.Pollution;
    TestTrue(TEXT("Empty history uses the configured pollution fallback"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::ReturnLostSecretBook, 1) }, TEXT("tide"), -1, false, Error));
    TestEqual(TEXT("Empty-history fallback amount"), State.Pollution, PollutionBefore + Catalog.Rules.ReturnedBookFallbackPollution);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreeUnlockTargetReleaseTest,
    "Bookstore.ProgramA.ReleaseDecrees.UnlockKeepsUnalteredCopiesSealed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreeUnlockTargetReleaseTest::RunTest(const FString& Parameters)
{
    using namespace ShopDecreeReleaseTests;
    const FName BookId(TEXT("TEST_secret"));
    const FName LostTargetId(TEXT("TEST_lost_shelf_target"));
    FShopCatalog Catalog;
    FShopRunState State;
    State.Random.Initialize(781);
    AddSecret(Catalog, State, BookId, 3);
    AddSecret(Catalog, State, LostTargetId, 1);
    FBookRuntime& Book = State.Inventory.FindChecked(BookId);
    // An older altered copy has already been released; the current target was
    // removed by sale or destruction. The remaining normal copies stay sealed.
    Book.SecretCopies[0].bAltered = true;
    Book.SecretCopies[0].bSealed = false;
    ShopEconomy::RefreshBookCounts(Book);
    FText Error;
    FShopEffect AlterTarget = Effect(EShopEffectType::AlterSecretBook, 1);
    AlterTarget.TargetId = LostTargetId;
    TestTrue(TEXT("Current cycle alters its owned target"), ShopEffects::Apply(State, Catalog,
        { AlterTarget }, TEXT("shelf"), 2, true, Error));
    FShopEffect DestroyTarget = Effect(EShopEffectType::DestroySecretBooks, 1);
    DestroyTarget.TargetId = LostTargetId;
    TestTrue(TEXT("Current target is destroyed before the loophole"), ShopEffects::Apply(State, Catalog,
        { DestroyTarget }, TEXT("tide"), 2, true, Error));
    TestEqual(TEXT("Current target is no longer in inventory"), State.Inventory[LostTargetId].Stock, 0);
    State.PendingPollutionBonus = 5;
    TestTrue(TEXT("Missing current target does not reject the loophole"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::UnlockSecretBook, 1), Effect(EShopEffectType::Pollution, 10) }, TEXT("shelf"), -1, false, Error));
    TestTrue(TEXT("First unaltered copy remains sealed"), Book.SecretCopies[1].bSealed);
    TestTrue(TEXT("Second unaltered copy remains sealed"), Book.SecretCopies[2].bSealed);
    TestEqual(TEXT("Missing target still applies pollution and bonus once"), State.Pollution, 15);
    TestEqual(TEXT("Missing target consumes pending pollution bonus"), State.PendingPollutionBonus, 0);

    // A new cycle can release its remaining altered sealed copy, while the old
    // unsealed copy and another ordinary sealed copy stay unchanged.
    Book.SecretCopies[2].bAltered = true;
    ShopEconomy::RefreshBookCounts(Book);
    TestTrue(TEXT("Later cycle releases an eligible altered copy"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::UnlockSecretBook, 1) }, TEXT("shelf"), -1, false, Error));
    TestFalse(TEXT("Current altered sealed copy is released"), Book.SecretCopies[2].bSealed);
    TestTrue(TEXT("Unaltered copy remains sealed across cycles"), Book.SecretCopies[1].bSealed);
    TestEqual(TEXT("Unlock itself never adds pollution"), State.Pollution, 15);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreeModifiersReleaseTest,
    "Bookstore.ProgramA.ReleaseDecrees.ModifierDedupAndSpecialBacklash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreeModifiersReleaseTest::RunTest(const FString& Parameters)
{
    using namespace ShopDecreeReleaseTests;
    FShopCatalog Catalog;
    FShopRunState State;
    State.Random.Initialize(17);
    State.Phase = EGamePhase::Calm;
    State.ResumePhase = EGamePhase::NightShop;
    State.Customers.SetNum(3);
    State.ActiveCustomer = 0;
    State.Customers[1].bFake = true;
    FText Error;
    for (int32 Repeat = 0; Repeat < 2; ++Repeat)
    {
        TestTrue(TEXT("Watch effects apply"), ShopEffects::Apply(State, Catalog,
            { Effect(EShopEffectType::NightlyMoney, -30, -1), Effect(EShopEffectType::NightlyPollution, 3, -1) }, TEXT("watch"), -1, false, Error));
        TestTrue(TEXT("Door current-night cost applies"), ShopEffects::Apply(State, Catalog,
            { Effect(EShopEffectType::CustomerCountDelta, -1) }, TEXT("door"), 2, true, Error));
    }
    TestEqual(TEXT("Recurring fee is not stacked on re-enactment"), ShopEffects::Sum(State, EShopEffectType::NightlyMoney), -30);
    TestEqual(TEXT("Recurring backlash is not stacked"), ShopEffects::Sum(State, EShopEffectType::NightlyPollution), 3);
    TestTrue(TEXT("Last eligible night storefront customer is removed"), State.Customers[2].bServed);
    TestFalse(TEXT("Active customer is preserved"), State.Customers[0].bServed);
    TestFalse(TEXT("Fake customer is preserved"), State.Customers[1].bServed);
    TestTrue(TEXT("Fake, decay and reset effects enqueue work"), ShopEffects::Apply(State, Catalog,
        { Effect(EShopEffectType::SpawnFakeCustomer, 1), Effect(EShopEffectType::SkipNightDecay, 1),
          Effect(EShopEffectType::ResetPollutionThresholds) }, TEXT("backlash"), -1, false, Error));
    TestEqual(TEXT("One fake customer is pending"), State.PendingFakeCustomers, 1);
    TestEqual(TEXT("One night skips decay"), State.SkipDecayNights, 1);
    TestTrue(TEXT("Threshold reset is requested"), State.StageResetPending);
    for (int32 Repeat = 0; Repeat < 20; ++Repeat)
        TestTrue(TEXT("Permanent business penalty applies safely"), ShopEffects::Apply(State, Catalog,
            { Effect(EShopEffectType::PermanentBusinessPenalty) }, TEXT("nameless"), -1, false, Error));
    TestEqual(TEXT("Rent penalty stops at configured cap"), State.RentPenalty, Catalog.Rules.MaxRentPenalty);
    TestEqual(TEXT("Customer penalty stops at configured cap"), State.CustomerPenalty, Catalog.Rules.MaxCustomerPenalty);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
