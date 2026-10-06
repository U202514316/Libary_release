#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace ShopRunReleaseTests
{
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner{NewObject<UGameInstance>()};
        TStrongObjectPtr<UShopRunSubsystem> Run{NewObject<UShopRunSubsystem>(Owner.Get())};
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        FRunRules Rules;
        FFixture()
        {
            const TCHAR* Names[] = {TEXT("DT_Books"), TEXT("DT_Customers"), TEXT("DT_RunRules"), TEXT("DT_Decrees"),
                TEXT("DT_Events"), TEXT("DT_MarketItems"), TEXT("DT_Owl"), TEXT("DT_Endings")};
            for (const TCHAR* Name : Names)
            {
                const FString Path = FString(TEXT("/Game/ProgramA/Release/Data/")) + Name + TEXT(".") + Name;
                UDataTable* Loaded = LoadObject<UDataTable>(nullptr, *Path);
                Tables.Emplace(Loaded ? DuplicateObject<UDataTable>(Loaded, Owner.Get()) : nullptr);
            }
            if (Tables[2].IsValid())
                if (const FRunRules* Row = Tables[2]->FindRow<FRunRules>(TEXT("Default"), TEXT("ReleaseTests"))) Rules = *Row;
        }
        ~FFixture() { Run->Deinitialize(); }
        bool Start()
        {
            for (const auto& Table : Tables) if (!Table.IsValid()) return false;
            Tables[2]->AddRow(TEXT("Default"), Rules);
            return Run->ConfigureTables(Tables[0].Get(), Tables[1].Get(), Tables[2].Get(), Tables[3].Get(), Tables[4].Get(),
                Tables[5].Get(), Tables[6].Get(), 54321, Tables[7].Get()) && Run->RequestNewRun_Implementation();
        }
        FRunSnapshot Snapshot() const { return Run->GetSnapshot_Implementation(); }
        bool Inside()
        {
            return Run->RequestEndDay_Implementation() && Run->RequestContinue_Implementation() && Run->RequestOpenInside_Implementation().bSucceeded;
        }
        bool ClearCalm()
        {
            // Explicit refusal is an authored player action; it must always close the modal.
            return Snapshot().Phase != EGamePhase::Calm || Run->RequestSkipDecree_Implementation().bSucceeded;
        }
    };
}
using namespace ShopRunReleaseTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReleaseOwnedStart, "Bookstore.ProgramA.ReleaseFlow.OwnedStartAndDeferredHistory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReleaseOwnedStart::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Load reviewed eight-table configuration"), F.Start())) return false;
    TestEqual(TEXT("No prototype psychic grant"), F.Snapshot().Psychic, 0);
    TestEqual(TEXT("Sixteen books loaded"), F.Run->GetBookIds().Num(), 16);
    TestTrue(TEXT("Enter inside shop"), F.Inside());
    FBookRuntime Book;
    TestTrue(TEXT("Read owned runtime"), F.Run->GetBookRuntime(TEXT("book_secret_01"), Book));
    TestEqual(TEXT("Initial book is already owned"), Book.Stock, 1);
    TestEqual(TEXT("Owned secret represented by one copy"), Book.SecretCopies.Num(), 1);
    TestTrue(TEXT("Zero psychic can read initially owned book"), F.Run->RequestReadSecret_Implementation(TEXT("book_secret_01")).bSucceeded);
    TestEqual(TEXT("Earn eight psychic"), F.Snapshot().Psychic, 8);
    TestTrue(TEXT("History remains disabled"), F.Snapshot().PendingEventId.IsNone());
    TestFalse(TEXT("Explicit history command cannot execute while deferred"), F.Run->RequestHistoryChoice_Implementation(EHistoryChoice::Witness).bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReleaseStageAndFallback, "Bookstore.ProgramA.ReleaseFlow.StagePointAndEmergency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReleaseStageAndFallback::RunTest(const FString&)
{
    FFixture F;
    // Exercise an existing high-pollution run; initialization itself is not a turn.
    F.Rules.StartPollution = 60;
    F.Rules.StartPsychic = 0;
    if (!TestTrue(TEXT("Start high-pollution zero-psychic scenario"), F.Start())) return false;
    TestEqual(TEXT("Startup does not advance turns"), F.Snapshot().Turn, 0);
    TestEqual(TEXT("Zero-psychic fallback is present"), F.Snapshot().DecreeCandidates.Num(), 1);
    FDecreeData Fallback;
    TestTrue(TEXT("Fallback details available to UI"), F.Run->GetDecreeInfo(TEXT("emergency_calm"), Fallback));
    TestTrue(TEXT("Fallback is marked"), Fallback.bFallback);
    const int32 MoneyBefore = F.Snapshot().Money;
    TestTrue(TEXT("Emergency action executes"), F.Run->RequestEnactDecree_Implementation(TEXT("emergency_calm")).bSucceeded);
    TestEqual(TEXT("Emergency charges configured money"), F.Snapshot().Money, MoneyBefore - F.Rules.EmergencyMoneyCost);
    TestEqual(TEXT("Emergency does not consume a turn"), F.Snapshot().Turn, 0);
    TestTrue(TEXT("Enter inside"), F.Inside());
    TestTrue(TEXT("Read first book to 60"), F.Run->RequestReadSecret_Implementation(TEXT("book_secret_01")).bSucceeded);
    TestTrue(TEXT("Read second book across Medium"), F.Run->RequestReadSecret_Implementation(TEXT("book_secret_02")).bSucceeded);
    TestEqual(TEXT("One upward crossing advances exactly once"), F.Snapshot().Turn, 1);
    TestEqual(TEXT("Crossing opens calm"), F.Snapshot().Phase, EGamePhase::Calm);
    const auto Before = F.Run->GetCustomers_Implementation();
    F.Run->Tick(10.f);
    const auto After = F.Run->GetCustomers_Implementation();
    if (Before.Num() && After.Num()) TestEqual(TEXT("Modal pauses patience"), After[0].Patience, Before[0].Patience);
    TestTrue(TEXT("Skip restores inside"), F.ClearCalm());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReleaseThirtyFiveDays, "Bookstore.ProgramA.ReleaseFlow.ThirtyFiveDaysAndTableEnding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReleaseThirtyFiveDays::RunTest(const FString&)
{
    FFixture F;
    // Isolate calendar/ending transitions from bankruptcy; game data is duplicated,
    // never modified on disk. Economy and sale probabilities have separate tests.
    F.Rules.StartMoney = 10000;
    if (!TestTrue(TEXT("Start calendar scenario"), F.Start())) return false;
    for (int32 Day = 1; Day <= 35; ++Day)
    {
        if (!TestEqual(TEXT("Expected day"), F.Snapshot().Day, Day)) return false;
        if (!TestTrue(TEXT("DayEnd -> Dusk -> Inside"), F.Inside())) return false;
        if (Day == 1)
        {
            TestTrue(TEXT("Actual gameplay reads an owned book"), F.Run->RequestReadSecret_Implementation(TEXT("book_secret_01")).bSucceeded);
            const int32 Index = 0;
            TestEqual(TEXT("Open inside sale"), F.Run->RequestBeginSell_Implementation(Index), EShopActionResult::Opened);
            TestEqual(TEXT("Sell to a nighttime customer"), F.Run->RequestSell_Implementation(TEXT("book_secret_01")), EShopActionResult::Sold);
        }
        if (!F.ClearCalm()) return false;
        if (!TestTrue(TEXT("Exactly one night settlement"), F.Run->RequestEndNight_Implementation().bSucceeded)) return false;
        if (!F.ClearCalm()) return false;
        TestFalse(TEXT("Cannot settle same night again"), F.Run->RequestEndNight_Implementation().bSucceeded);
        if (!TestTrue(TEXT("Advance or end"), F.Run->RequestNextDay_Implementation())) return false;
    }
    TestEqual(TEXT("No day 36"), F.Snapshot().Day, 35);
    TestEqual(TEXT("Final phase"), F.Snapshot().Phase, EGamePhase::End);
    TestEqual(TEXT("Low-enlightenment ending"), F.Snapshot().Ending, EShopEnding::Cycle);
    FEndingData Ending;
    TestTrue(TEXT("Ending is table-configured"), F.Run->GetEndingInfo(EShopEnding::Cycle, Ending));
    TestEqual(TEXT("UI receives authored ending text"), F.Snapshot().EndMessage.ToString(), Ending.Text.ToString());
    TestTrue(TEXT("New run resets after ending"), F.Run->RequestNewRun_Implementation());
    TestEqual(TEXT("Reset day"), F.Snapshot().Day, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReleaseFakeCustomer, "Bookstore.ProgramA.ReleaseFlow.FakeCustomerLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReleaseFakeCustomer::RunTest(const FString&)
{
    FFixture F;
    F.Rules.StartMoney = 10000;
    F.Rules.StartPsychic = 40;
    F.Rules.StartPollution = 31;
    if (!TestTrue(TEXT("Start with affordable copper decrees"), F.Start())) return false;
    if (!TestTrue(TEXT("Enact closed-door decree"), F.Run->RequestEnactDecree_Implementation(TEXT("bronze_02")).bSucceeded)) return false;
    TestEqual(TEXT("Enactment does not consume its lifetime"), F.Snapshot().Turn, 0);
    for (int32 Night = 1; Night <= 2; ++Night)
    {
        if (!TestTrue(TEXT("Enter inside for actual settlement"), F.Inside())) return false;
        if (!TestTrue(TEXT("Settle one point"), F.Run->RequestEndNight_Implementation().bSucceeded)) return false;
        if (!F.ClearCalm() || !F.Run->RequestNextDay_Implementation()) return false;
    }
    for (const FCustomerRuntime& Customer : F.Run->GetCustomers_Implementation())
        TestFalse(TEXT("Pending fake never appears in the surface shop"), Customer.bFake);
    if (!TestTrue(TEXT("Enter next inside shop"), F.Inside())) return false;
    const auto Customers = F.Run->GetCustomers_Implementation();
    TestEqual(TEXT("One fake joins the regular inside queue"), Customers.Num(), F.Rules.InsideCustomers + 1);
    if (!TestTrue(TEXT("Fake is inserted after current customer"), Customers.IsValidIndex(1) && Customers[1].bFake)) return false;
    TestTrue(TEXT("Resolve regular queue head"), F.Run->RequestRejectCustomer_Implementation(0).bSucceeded);
    TestEqual(TEXT("Fake cannot buy a book"), F.Run->RequestBeginSell_Implementation(1), EShopActionResult::Unavailable);
    TestTrue(TEXT("Player can inspect fake"), F.Run->RequestObserveCustomer_Implementation(1).bSucceeded);
    TestTrue(TEXT("Observation is recorded"), F.Run->GetCustomers_Implementation()[1].bObserved);
    const int32 PollutionBefore = F.Snapshot().Pollution;
    F.Run->Tick(1.1f);
    TestEqual(TEXT("Waiting one second adds configured pollution"), F.Snapshot().Pollution, PollutionBefore + F.Rules.FakeCustomerPollutionPerSecond);
    TestTrue(TEXT("Player can dismiss fake"), F.Run->RequestRejectCustomer_Implementation(1).bSucceeded);
    F.Run->Tick(2.f);
    TestEqual(TEXT("Dismissed fake stops emitting pollution"), F.Snapshot().Pollution, PollutionBefore + F.Rules.FakeCustomerPollutionPerSecond);
    return true;
}
#endif
