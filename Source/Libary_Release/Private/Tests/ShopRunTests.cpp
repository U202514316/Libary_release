#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopRunSubsystem.h"
#include "ShopTypes.h"
#include "ShopStory.h"
#include "ShopDecrees.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace ShopRunTests
{
    // These TEST_* records are test fixtures, not authored game content. All runtime
    // changes go through the same service commands exposed to Blueprint callers.
    const FName Novel(TEXT("TEST_novel"));
    const FName Poem(TEXT("TEST_poem"));
    const FName Secret(TEXT("TEST_secret"));
    const FName Decree(TEXT("TEST_decree_0"));

    FShopEffect Resource(EShopEffectType Type, int32 Amount)
    {
        FShopEffect Value;
        Value.Type = Type;
        Value.Amount = Amount;
        return Value;
    }

    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner;
        TStrongObjectPtr<UShopRunSubsystem> Shop;
        TArray<TStrongObjectPtr<UDataTable>> OwnedTables;
        UDataTable* Books = nullptr;
        UDataTable* Customers = nullptr;
        UDataTable* RulesTable = nullptr;
        UDataTable* Decrees = nullptr;
        UDataTable* Events = nullptr;
        UDataTable* Market = nullptr;
        UDataTable* Owl = nullptr;
        FRunRules Rules;

        template<typename T> UDataTable* MakeTable()
        {
            UDataTable* Table = NewObject<UDataTable>(Owner.Get());
            Table->RowStruct = T::StaticStruct();
            OwnedTables.Emplace(Table);
            return Table;
        }

        FFixture() : Owner(NewObject<UGameInstance>()), Shop(NewObject<UShopRunSubsystem>(Owner.Get()))
        {
            Books = MakeTable<FBookData>();
            Customers = MakeTable<FCustomerData>();
            RulesTable = MakeTable<FRunRules>();
            Decrees = MakeTable<FDecreeData>();
            Events = MakeTable<FEventData>();
            Market = MakeTable<FMarketItemData>();
            Owl = MakeTable<FOwlLine>();

            FBookData Book;
            Book.DisplayName = FText::FromString(TEXT("TEST novel"));
            Book.InitialStock = 2;
            Books->AddRow(Novel, Book);
            Book.DisplayName = FText::FromString(TEXT("TEST poem"));
            Book.BookType = EBookType::Poem;
            Book.InitialStock = 1;
            Books->AddRow(Poem, Book);
            Book.DisplayName = FText::FromString(TEXT("TEST secret"));
            Book.BookType = EBookType::Secret;
            Book.Layer = EBookLayer::Inside;
            Book.Cost = 0;
            Book.Price = 60;
            // Release contract: InitialStock is owned in both layers. Offers are separate.
            Book.InitialStock = 1;
            Book.InitialOwnedStock = 0;
            Book.CollectOfferPerNight = 1;
            Books->AddRow(Secret, Book);

            FCustomerData Customer;
            Customer.DisplayName = FText::FromString(TEXT("TEST normal"));
            Customer.NeedLine = FText::FromString(TEXT("TEST {类型}"));
            Customers->AddRow(TEXT("TEST_normal"), Customer);
            Customer.DisplayName = FText::FromString(TEXT("TEST inside"));
            Customer.Kind = ECustomerKind::Secret;
            Customers->AddRow(TEXT("TEST_inside"), Customer);
            for (int32 Index = 0; Index < 3; ++Index)
            {
                FDecreeData Row;
                Row.Id = FName(*FString::Printf(TEXT("TEST_decree_%d"), Index));
                Row.DisplayName = FText::FromString(TEXT("TEST decree"));
                Row.PsychicCost = 8;
                Row.PollutionCut = 0;
                Decrees->AddRow(Row.Id, Row);
            }
            // These scenarios need funds for decree transactions; the actual release
            // default is zero and is covered by the separate release-contract tests.
            Rules.StartPsychic = 24;
            Rules.CustomersMin = Rules.CustomersMax = 3;
            Rules.WeekTwoCustomerBonus = 0;
            Rules.InsideCustomers = 2;
            Rules.RandomNeedPool = {EBookType::Novel};
            Rules.Rent = 0;
            Rules.PollutionDecay = 0;
            // Isolate the existing exact-delta assertions from the newly authored
            // loose-book spread mechanic, which has its own release-flow tests.
            Rules.LightSpreadPerNight = 0;
            Rules.DecreeCandidateCount = 3;
            Rules.bEnableMarket = false;
        }

        ~FFixture() { Shop->Deinitialize(); }

        bool Configure()
        {
            RulesTable->EmptyTable();
            RulesTable->AddRow(TEXT("Default"), Rules);
            return Shop->ConfigureTables(Books, Customers, RulesTable, Decrees, Events, Market, Owl, 12345);
        }
        bool Start() { return Configure() && IShopService::Execute_RequestNewRun(Shop.Get()); }
        FRunSnapshot Snapshot() const { return IShopService::Execute_GetSnapshot(Shop.Get()); }
        TArray<FCustomerRuntime> Queue() const { return IShopService::Execute_GetCustomers(Shop.Get()); }
        FBookRuntime Book(FName Id) const
        {
            FBookRuntime Result;
            Shop->GetBookRuntime(Id, Result);
            return Result;
        }
        bool OpenNight(bool bInside)
        {
            if (!IShopService::Execute_RequestEndDay(Shop.Get()) || !IShopService::Execute_RequestContinue(Shop.Get())) return false;
            return bInside ? IShopService::Execute_RequestOpenInside(Shop.Get()).bSucceeded : IShopService::Execute_RequestOpenRestock(Shop.Get()).bSucceeded;
        }
        bool Settle() { return IShopService::Execute_RequestEndNight(Shop.Get()).bSucceeded; }
        bool Continue() { return IShopService::Execute_RequestContinue(Shop.Get()); }
    };

    bool Phase(FAutomationTestBase& Test, const FFixture& F, EGamePhase Expected, const TCHAR* Label)
    {
        return Test.TestEqual(Label, static_cast<int32>(F.Snapshot().Phase), static_cast<int32>(Expected));
    }

    bool Unchanged(FAutomationTestBase& Test, const FRunSnapshot& Before, const FRunSnapshot& After)
    {
        bool Good = Test.TestEqual(TEXT("Rejected request preserves money"), After.Money, Before.Money);
        Good &= Test.TestEqual(TEXT("Rejected request preserves psychic"), After.Psychic, Before.Psychic);
        Good &= Test.TestEqual(TEXT("Rejected request preserves pollution"), After.Pollution, Before.Pollution);
        Good &= Test.TestEqual(TEXT("Rejected request preserves enlightenment"), After.Enlighten, Before.Enlighten);
        Good &= Test.TestEqual(TEXT("Rejected request preserves stock"), After.TotalStock, Before.TotalStock);
        Good &= Test.TestEqual(TEXT("Rejected request preserves income"), After.TodayIncome, Before.TodayIncome);
        Good &= Test.TestEqual(TEXT("Rejected request preserves expense"), After.TodayExpense, Before.TodayExpense);
        Good &= Test.TestEqual(TEXT("Rejected request preserves sold count"), After.TotalSold, Before.TotalSold);
        Good &= Test.TestEqual(TEXT("Rejected request preserves logical turn"), After.Turn, Before.Turn);
        Good &= Test.TestEqual(TEXT("Rejected request preserves phase"), static_cast<int32>(After.Phase), static_cast<int32>(Before.Phase));
        Good &= Test.TestEqual(TEXT("Rejected request preserves decree count"), After.ActiveDecrees.Num(), Before.ActiveDecrees.Num());
        Good &= Test.TestEqual(TEXT("Rejected request preserves clues"), After.Clues.Num(), Before.Clues.Num());
        return Good;
    }
}

using namespace ShopRunTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopConfigValidationTest, "Bookstore.ProgramA.ConfigRejectsInvalidData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopConfigValidationTest::RunTest(const FString& Parameters)
{
    {
        FFixture F;
        F.Rules.RandomNeedPool.Reset();
        TestFalse(TEXT("Empty need pool cannot configure/start a run"), F.Start());
        Phase(*this, F, EGamePhase::Boot, TEXT("Rejected configuration leaves Boot"));
        TestEqual(TEXT("Invalid config does not start day one"), F.Snapshot().Day, 0);
    }
    {
        FFixture F;
        F.Rules.CustomersMin = 6;
        F.Rules.CustomersMax = 2;
        TestFalse(TEXT("Reversed customer count range rejected"), F.Start());
    }
    {
        FFixture F;
        F.Rules.LightThreshold = F.Rules.MediumThreshold;
        TestFalse(TEXT("Nonincreasing pollution thresholds rejected"), F.Start());
    }
    {
        FFixture F;
        F.Rules.bEnableMarket = true;
        TestFalse(TEXT("Enabled market requires authored items"), F.Start());
    }
    {
        FFixture F;
        F.Rules.bRequireFinalContentCounts = true;
        TestFalse(TEXT("Prototype records cannot claim final content completeness"), F.Start());
    }
    {
        FFixture F;
        F.Customers->FindRow<FCustomerData>(TEXT("TEST_inside"), TEXT("TEST"))->MinPollution = 61;
        // Inside demand no longer requires a Secret-role visitor: Normal/Hurry may
        // visit inside too. Gating only the Secret role must not block entry.
        if (!TestTrue(TEXT("Normal visitors keep inside enterable at low pollution"), F.Start())) return false;
        if (!TestTrue(TEXT("Inside still opens without an eligible Secret-role visitor"), F.OpenNight(true))) return false;
        for (const FCustomerRuntime& Customer : F.Queue())
        {
            TestEqual(TEXT("Eligible normal visitor can use the inside shop"), static_cast<int32>(Customer.Kind), static_cast<int32>(ECustomerKind::Normal));
            TestEqual(TEXT("Role does not change inside book demand"), static_cast<int32>(Customer.NeedType), static_cast<int32>(EBookType::Secret));
        }
    }
    {
        FFixture F;
        F.Customers->FindRow<FCustomerData>(TEXT("TEST_normal"), TEXT("TEST"))->MinPollution = 61;
        F.Customers->FindRow<FCustomerData>(TEXT("TEST_inside"), TEXT("TEST"))->MinPollution = 61;
        TestFalse(TEXT("A catalog with no low-pollution visitors is still rejected"), F.Start());
    }
    {
        FFixture F;
        F.Books->EmptyTable();
        F.Books->RowStruct = FCustomerData::StaticStruct();
        TestFalse(TEXT("Wrong native row structure rejected"), F.Start());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTransactionTest, "Bookstore.ProgramA.TransactionsAndQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopTransactionTest::RunTest(const FString& Parameters)
{
    FFixture F;
    if (!TestTrue(TEXT("Valid test configuration starts"), F.Start())) return false;
    const FRunSnapshot Start = F.Snapshot();
    TestFalse(TEXT("Restock is unavailable during Day"), IShopService::Execute_RequestRestock(F.Shop.Get(), Novel));
    Unchanged(*this, Start, F.Snapshot());
    TestTrue(TEXT("Cannot jump past the first queued customer"), IShopService::Execute_RequestBeginSell(F.Shop.Get(), 1) != EShopActionResult::Opened);
    TestEqual(TEXT("First customer opens"), static_cast<int32>(IShopService::Execute_RequestBeginSell(F.Shop.Get(), 0)), static_cast<int32>(EShopActionResult::Opened));
    const FRunSnapshot Opened = F.Snapshot();
    TestTrue(TEXT("Unknown BookId does not sell"), IShopService::Execute_RequestSell(F.Shop.Get(), TEXT("TEST_missing")) != EShopActionResult::Sold);
    Unchanged(*this, Opened, F.Snapshot());
    TestFalse(TEXT("Unknown book does not consume customer"), F.Queue()[0].bServed);
    TestFalse(TEXT("Cannot close while sale is open"), IShopService::Execute_RequestEndDay(F.Shop.Get()));
    Unchanged(*this, Opened, F.Snapshot());
    TestEqual(TEXT("Wrong type reports WrongBook"), static_cast<int32>(IShopService::Execute_RequestSell(F.Shop.Get(), Poem)), static_cast<int32>(EShopActionResult::WrongBook));
    TestEqual(TEXT("Wrong book preserves money"), F.Snapshot().Money, Start.Money);
    TestEqual(TEXT("Wrong book preserves inventory"), F.Snapshot().TotalStock, Start.TotalStock);
    TestTrue(TEXT("Configured wrong-book policy resolves customer"), F.Queue()[0].bServed);
    if (!TestEqual(TEXT("Next queued customer opens"), static_cast<int32>(IShopService::Execute_RequestBeginSell(F.Shop.Get(), 1)), static_cast<int32>(EShopActionResult::Opened))) return false;
    TestEqual(TEXT("Matching book sells"), static_cast<int32>(IShopService::Execute_RequestSell(F.Shop.Get(), Novel)), static_cast<int32>(EShopActionResult::Sold));
    TestEqual(TEXT("Price paid exactly once"), F.Snapshot().Money, Start.Money + 20);
    TestEqual(TEXT("One copy removed"), F.Book(Novel).Stock, 1);
    TestEqual(TEXT("One successful sale counted"), F.Snapshot().TotalSold, 1);
    const FRunSnapshot Sold = F.Snapshot();
    TestTrue(TEXT("Duplicate sell cannot commit twice"), IShopService::Execute_RequestSell(F.Shop.Get(), Novel) != EShopActionResult::Sold);
    Unchanged(*this, Sold, F.Snapshot());
    if (!TestEqual(TEXT("Last customer opens"), static_cast<int32>(IShopService::Execute_RequestBeginSell(F.Shop.Get(), 2)), static_cast<int32>(EShopActionResult::Opened))) return false;
    TestTrue(TEXT("Cancel succeeds"), IShopService::Execute_RequestCancelSell(F.Shop.Get()));
    TestFalse(TEXT("Cancel leaves customer available"), F.Queue()[2].bServed);
    TestEqual(TEXT("Cancelled customer can reopen"), static_cast<int32>(IShopService::Execute_RequestBeginSell(F.Shop.Get(), 2)), static_cast<int32>(EShopActionResult::Opened));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPhaseSettlementTest, "Bookstore.ProgramA.ExclusiveNightAndSingleSettlement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPhaseSettlementTest::RunTest(const FString& Parameters)
{
    for (ERentTiming Timing : {ERentTiming::BeforeDusk, ERentTiming::NightEnd})
    {
        FFixture F;
        F.Rules.Rent = 25;
        F.Rules.RentTiming = Timing;
        if (!TestTrue(TEXT("Start"), F.Start())) return false;
        TestTrue(TEXT("End day"), IShopService::Execute_RequestEndDay(F.Shop.Get()));
        Phase(*this, F, EGamePhase::DayEnd, TEXT("DayEnd awaits continue"));
        const FRunSnapshot DayEnd = F.Snapshot();
        TestFalse(TEXT("Cannot repeat day-end"), IShopService::Execute_RequestEndDay(F.Shop.Get()));
        Unchanged(*this, DayEnd, F.Snapshot());
        TestTrue(TEXT("Continue to dusk"), F.Continue());
        Phase(*this, F, EGamePhase::DuskChoice, TEXT("Dusk choice is explicit"));
        TestTrue(TEXT("Choose restock"), IShopService::Execute_RequestOpenRestock(F.Shop.Get()).bSucceeded);
        TestFalse(TEXT("Cannot also choose inside"), IShopService::Execute_RequestOpenInside(F.Shop.Get()).bSucceeded);
        TestFalse(TEXT("Cannot skip night settlement"), IShopService::Execute_RequestNextDay(F.Shop.Get()));
        TestTrue(TEXT("Restock one novel"), IShopService::Execute_RequestRestock(F.Shop.Get(), Novel));
        TestEqual(TEXT("Exactly one copy purchased"), F.Book(Novel).Stock, 3);
        if (!TestTrue(TEXT("Settle night"), F.Settle())) return false;
        Phase(*this, F, EGamePhase::NightEnd, TEXT("NightEnd is displayed"));
        TestEqual(TEXT("One rent and one purchase deducted"), F.Snapshot().Money, 65);
        TestEqual(TEXT("Expense contains rent and purchase once"), F.Snapshot().TodayExpense, 35);
        TestEqual(TEXT("One night advances one turn"), F.Snapshot().Turn, 1);
        const FRunSnapshot Settled = F.Snapshot();
        TestFalse(TEXT("Repeated settlement rejected"), F.Settle());
        Unchanged(*this, Settled, F.Snapshot());
        TestTrue(TEXT("Continue after night"), F.Continue());
        Phase(*this, F, EGamePhase::Day, TEXT("Day two starts"));
        TestEqual(TEXT("Next day increments once"), F.Snapshot().Day, 2);
        TestEqual(TEXT("Next day clears daily expenses"), F.Snapshot().TodayExpense, 0);
        TestFalse(TEXT("A second new run is blocked while running"), IShopService::Execute_RequestNewRun(F.Shop.Get()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPatienceModalTest, "Bookstore.ProgramA.PatienceAndModalPause", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPatienceModalTest::RunTest(const FString& Parameters)
{
    {
        FFixture F;
        if (!TestTrue(TEXT("Start patience fixture"), F.Start())) return false;
        F.Shop->Tick(5.f);
        TestEqual(TEXT("First queued customer loses patience"), F.Queue()[0].Patience, 25.f);
        TestEqual(TEXT("Waiting customer does not lose patience"), F.Queue()[1].Patience, 30.f);
        IShopService::Execute_RequestBeginSell(F.Shop.Get(), 0);
        F.Shop->Tick(3.f);
        TestEqual(TEXT("Selling still consumes patience"), F.Queue()[0].Patience, 22.f);
        IShopService::Execute_RequestCancelSell(F.Shop.Get());
        F.Shop->Tick(100.f);
        TestTrue(TEXT("Expired current customer resolves"), F.Queue()[0].bServed);
        TestEqual(TEXT("Timeout reports Expired"), static_cast<int32>(F.Queue()[0].Resolution), static_cast<int32>(EShopActionResult::Expired));
        TestEqual(TEXT("Large delta does not expire the next customer"), F.Queue()[1].Patience, 30.f);
    }
    {
        FFixture F;
        // Historical content is deliberately off in the release. This regression
        // opts in solely to continue verifying the authored-test modal lifecycle.
        F.Rules.bEnableHistory = true;
        FEventData Event;
        Event.Id = TEXT("TEST_history");
        Event.Trigger = EShopEventTrigger::OnRead;
        Event.RequiredBookId = Secret;
        Event.Text = FText::FromString(TEXT("TEST history, not game dialogue"));
        Event.OptionA = FText::FromString(TEXT("Witness"));
        Event.ResultA.Add(Resource(EShopEffectType::Enlighten, 5));
        F.Events->AddRow(Event.Id, Event);
        if (!TestTrue(TEXT("Start history fixture"), F.Start()) || !TestTrue(TEXT("Choose inside"), F.OpenNight(true))) return false;
        F.Shop->Tick(3.f);
        const float Patience = F.Queue()[0].Patience;
        if (!TestTrue(TEXT("Read opens authored test event"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
        Phase(*this, F, EGamePhase::History, TEXT("History modal is active"));
        F.Shop->Tick(100.f);
        TestEqual(TEXT("History modal pauses patience"), F.Queue()[0].Patience, Patience);
        TestTrue(TEXT("Witness resumes"), IShopService::Execute_RequestHistoryChoice(F.Shop.Get(), EHistoryChoice::Witness).bSucceeded);
        Phase(*this, F, EGamePhase::Inside, TEXT("History resumes inside"));
        F.Shop->Tick(1.f);
        TestEqual(TEXT("Patience resumes after history"), F.Queue()[0].Patience, Patience - 1.f);
    }
    {
        FFixture F;
        F.Rules.StartPollution = 30;
        if (!TestTrue(TEXT("Start calm fixture"), F.Start()) || !TestTrue(TEXT("Choose inside"), F.OpenNight(true))) return false;
        if (!TestTrue(TEXT("Read crosses light threshold"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
        Phase(*this, F, EGamePhase::Calm, TEXT("Calm interrupts on upward stage transition"));
        const float Patience = F.Queue()[0].Patience;
        F.Shop->Tick(100.f);
        TestEqual(TEXT("Calm modal pauses patience"), F.Queue()[0].Patience, Patience);
        TestTrue(TEXT("Skip calm is available"), IShopService::Execute_RequestSkipDecree(F.Shop.Get()).bSucceeded);
        F.Shop->Tick(1.f);
        TestEqual(TEXT("Patience resumes after calm"), F.Queue()[0].Patience, Patience - 1.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDecreeDelayTest, "Bookstore.ProgramA.DecreeLoopholeAfterTwoNights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDecreeDelayTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.StartPollution = 30;
    // This test counts night-settlement TTL only. Stage-rise turn advancement is
    // independently exercised with release defaults by the new flow tests.
    F.Rules.bAdvanceTurnOnStageRise = false;
    FDecreeData* Rule = F.Decrees->FindRow<FDecreeData>(Decree, TEXT("TEST"));
    Rule->LoopholeDelay = 2;
    Rule->LoopholeEffect.Add(Resource(EShopEffectType::Pollution, 7));
    if (!TestTrue(TEXT("Start"), F.Start()) || !TestTrue(TEXT("Inside"), F.OpenNight(true))) return false;
    if (!TestTrue(TEXT("Read and open calm"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    TestTrue(TEXT("Test decree appears in complete candidate pool"), F.Snapshot().DecreeCandidates.Contains(Decree));
    if (!TestTrue(TEXT("Enact"), IShopService::Execute_RequestEnactDecree(F.Shop.Get(), Decree).bSucceeded)) return false;
    const FRunSnapshot Enacted = F.Snapshot();
    if (!TestEqual(TEXT("One decree runtime"), Enacted.ActiveDecrees.Num(), 1)) return false;
    TestEqual(TEXT("Not enacted on a future turn"), Enacted.ActiveDecrees[0].EnactedTurn, 0);
    TestEqual(TEXT("Loophole deadline is two turns away"), Enacted.ActiveDecrees[0].LoopholeAtTurn, 2);
    TestFalse(TEXT("Cannot enact twice outside calm"), IShopService::Execute_RequestEnactDecree(F.Shop.Get(), Decree).bSucceeded);
    Unchanged(*this, Enacted, F.Snapshot());
    F.Shop->Tick(1.f);
    TestEqual(TEXT("Real-time tick does not advance logical turns"), F.Snapshot().Turn, 0);
    if (!TestTrue(TEXT("First settlement"), F.Settle())) return false;
    TestFalse(TEXT("No loophole on first night"), F.Snapshot().ActiveDecrees[0].bLoopholeTriggered);
    TestEqual(TEXT("First night does not add penalty"), F.Snapshot().Pollution, Enacted.Pollution);
    if (!TestTrue(TEXT("Day two"), F.Continue()) || !TestTrue(TEXT("Restock night two"), F.OpenNight(false)) || !TestTrue(TEXT("Second settlement"), F.Settle())) return false;
    TestTrue(TEXT("Second night triggers loophole"), F.Snapshot().ActiveDecrees[0].bLoopholeTriggered);
    TestFalse(TEXT("Triggered decree is no longer active"), F.Snapshot().ActiveDecrees[0].bActive);
    TestEqual(TEXT("Default cooldown starts after the loophole at turn two"), F.Snapshot().ActiveDecrees[0].CooldownUntilTurn, 2 + F.Rules.DecreeCooldownTurns);
    TestEqual(TEXT("Penalty added exactly once"), F.Snapshot().Pollution, Enacted.Pollution + 7);
    if (!TestTrue(TEXT("Day three"), F.Continue()) || !TestTrue(TEXT("Restock night three"), F.OpenNight(false)) || !TestTrue(TEXT("Third settlement"), F.Settle())) return false;
    TestEqual(TEXT("Expired decree does not trigger again"), F.Snapshot().Pollution, Enacted.Pollution + 7);
    TestFalse(TEXT("A decree still cooling down is absent from the next candidate pool"), F.Snapshot().DecreeCandidates.Contains(Decree));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopEffectAtomicityTest, "Bookstore.ProgramA.DecreeCompositeCostIsAtomic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopEffectAtomicityTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.StartPollution = 30;
    FDecreeData* Rule = F.Decrees->FindRow<FDecreeData>(Decree, TEXT("TEST"));
    Rule->PollutionCut = 15;
    Rule->CostEffect.Add(Resource(EShopEffectType::Money, -10));
    // Reading supplies 32 psychic in this fixture. The extra 28 is affordable
    // alone, but base cost 8 + extra cost 28 is not: check the combined cost.
    Rule->CostEffect.Add(Resource(EShopEffectType::Psychic, -28));
    if (!TestTrue(TEXT("Start"), F.Start()) || !TestTrue(TEXT("Inside"), F.OpenNight(true))) return false;
    if (!TestTrue(TEXT("Read and open calm"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    const FRunSnapshot Before = F.Snapshot();
    TestEqual(TEXT("Candidate affordability trials do not spend money"), Before.Money, F.Rules.StartMoney);
    TestEqual(TEXT("Candidate affordability trials do not spend psychic"), Before.Psychic, F.Rules.StartPsychic + F.Rules.ReadPsychicGain);
    TestFalse(TEXT("Insufficient aggregate psychic rejects composite cost"), IShopService::Execute_RequestEnactDecree(F.Shop.Get(), Decree).bSucceeded);
    Unchanged(*this, Before, F.Snapshot());
    TestFalse(TEXT("New candidate policy excludes unaffordable aggregate costs"), F.Snapshot().DecreeCandidates.Contains(Decree));
    // Exercise the transaction preflight even if an old UI still has the card.
    // Merely testing an ID excluded above would only cover candidate membership.
    FShopCatalog StaleCatalog;
    StaleCatalog.Rules = F.Rules;
    StaleCatalog.Decrees.Add(Decree, *Rule);
    FShopRunState StaleState;
    StaleState.Money = Before.Money;
    StaleState.Psychic = Before.Psychic;
    StaleState.Pollution = Before.Pollution;
    StaleState.DecreeCandidates.Add(Decree);
    FText StaleError;
    TestFalse(TEXT("A stale candidate still rechecks the full composite cost"), ShopDecrees::Enact(StaleState, StaleCatalog, Decree, StaleError));
    TestEqual(TEXT("Rejected stale candidate does not partially deduct money"), StaleState.Money, Before.Money);
    TestEqual(TEXT("Rejected stale candidate does not partially deduct psychic"), StaleState.Psychic, Before.Psychic);
    TestEqual(TEXT("Rejected stale candidate does not apply pollution reduction"), StaleState.Pollution, Before.Pollution);
    TestEqual(TEXT("Rejected stale candidate creates no active decree"), StaleState.Decrees.Num(), 0);
    TestFalse(TEXT("Unknown decree cannot bypass candidate list"), IShopService::Execute_RequestEnactDecree(F.Shop.Get(), TEXT("TEST_missing")).bSucceeded);
    Unchanged(*this, Before, F.Snapshot());
    TestTrue(TEXT("Can leave calm after failure"), IShopService::Execute_RequestSkipDecree(F.Shop.Get()).bSucceeded);
    const FRunSnapshot ReadOnce = F.Snapshot();
    TestFalse(TEXT("Same owned copy cannot farm repeated reading"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded);
    Unchanged(*this, ReadOnce, F.Snapshot());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNegativeDaysResetTest, "Bookstore.ProgramA.ThreeNegativeDaysAndNewRunReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNegativeDaysResetTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.StartMoney = 0;
    F.Rules.Rent = 25;
    if (!TestTrue(TEXT("Start"), F.Start())) return false;
    for (int32 Day = 1; Day <= 3; ++Day)
    {
        if (!TestTrue(TEXT("Choose restock without spending"), F.OpenNight(false)) || !TestTrue(TEXT("Settle debt day"), F.Settle())) return false;
        TestEqual(TEXT("Negative balance streak counted once per night"), F.Snapshot().NegativeDays, Day);
        TestEqual(TEXT("Rent is charged once per day"), F.Snapshot().Money, -25 * Day);
        if (Day < 3)
        {
            TestEqual(TEXT("One/two debt days do not close store"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::None));
            if (!TestTrue(TEXT("Continue under grace policy"), F.Continue())) return false;
        }
    }
    Phase(*this, F, EGamePhase::End, TEXT("Third consecutive negative night ends run"));
    TestEqual(TEXT("Closure ending"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::Closed));
    if (!TestTrue(TEXT("NewRun restarts after ending"), IShopService::Execute_RequestNewRun(F.Shop.Get()))) return false;
    const FRunSnapshot Reset = F.Snapshot();
    Phase(*this, F, EGamePhase::Day, TEXT("Restart returns to day one"));
    TestEqual(TEXT("Day reset"), Reset.Day, 1);
    TestEqual(TEXT("Turn reset"), Reset.Turn, 0);
    TestEqual(TEXT("Debt streak reset"), Reset.NegativeDays, 0);
    TestEqual(TEXT("Money reset"), Reset.Money, F.Rules.StartMoney);
    TestEqual(TEXT("Psychic reset"), Reset.Psychic, F.Rules.StartPsychic);
    TestEqual(TEXT("Pollution reset"), Reset.Pollution, F.Rules.StartPollution);
    TestEqual(TEXT("Sales reset"), Reset.TotalSold, 0);
    TestEqual(TEXT("Decrees reset"), Reset.ActiveDecrees.Num(), 0);
    TestEqual(TEXT("Clues reset"), Reset.Clues.Num(), 0);
    TestEqual(TEXT("Starting novel inventory restored"), F.Book(Novel).Stock, 2);
    TestEqual(TEXT("Starting secret book is restored as owned inventory"), F.Book(Secret).Stock, 1);
    TestEqual(TEXT("Restart restores the per-copy secret inventory"), F.Book(Secret).SecretCopies.Num(), 1);
    TestEqual(TEXT("Restart clears per-copy reading progress"), F.Book(Secret).ReadCopies, 0);
    TestEqual(TEXT("Starting secret offers restored"), F.Book(Secret).AvailableToCollect, 1);
    TestFalse(TEXT("Fresh customer unserved"), F.Queue()[0].bServed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopEndingTest, "Bookstore.ProgramA.PollutionLimitAndDay35Endings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopEndingTest::RunTest(const FString& Parameters)
{
    {
        FFixture F;
        F.Rules.PollutionOnRead = 100;
        if (!TestTrue(TEXT("Start pollution-limit fixture"), F.Start()) || !TestTrue(TEXT("Inside"), F.OpenNight(true))) return false;
        TestTrue(TEXT("Read reaches terminal pollution"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded);
        Phase(*this, F, EGamePhase::End, TEXT("100 pollution ends immediately"));
        TestEqual(TEXT("Pollution release ending"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::PollutionReleased));
        const FRunSnapshot Ended = F.Snapshot();
        TestFalse(TEXT("Cannot purify out of a terminal ending"), IShopService::Execute_RequestPurify(F.Shop.Get(), 100).bSucceeded);
        Unchanged(*this, Ended, F.Snapshot());
    }
    struct FEndingCase { int32 Enlighten; int32 Pollution; EShopEnding Expected; };
    const FEndingCase Cases[] = {{60, 0, EShopEnding::Returned}, {59, 0, EShopEnding::Cycle}, {60, 60, EShopEnding::Cycle}};
    for (const FEndingCase& Case : Cases)
    {
        FFixture F;
        F.Rules.StartEnlighten = Case.Enlighten;
        F.Rules.StartPollution = Case.Pollution;
        if (!TestTrue(TEXT("Start final-day fixture"), F.Start())) return false;
        if (F.Snapshot().Phase == EGamePhase::Calm)
        {
            if (!TestTrue(TEXT("Acknowledge configured initial pollution stage"), IShopService::Execute_RequestSkipDecree(F.Shop.Get()).bSucceeded)) return false;
        }
        for (int32 Day = 1; Day <= 35; ++Day)
        {
            if (!TestTrue(TEXT("Open legal restock night"), F.OpenNight(false)) || !TestTrue(TEXT("Settle legal night"), F.Settle())) return false;
            if (Day < 35)
            {
                TestEqual(TEXT("Enlightenment alone cannot end before day 35"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::None));
                if (!TestTrue(TEXT("Continue to next day"), F.Continue())) return false;
            }
            else if (F.Snapshot().Phase == EGamePhase::NightEnd)
            {
                if (!TestTrue(TEXT("Continue final settlement"), F.Continue())) return false;
            }
        }
        TestEqual(TEXT("Final day is 35"), F.Snapshot().Day, 35);
        Phase(*this, F, EGamePhase::End, TEXT("Final night ends the run"));
        TestEqual(TEXT("Exact enlightenment and pollution boundary determines ending"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(Case.Expected));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopObserveRejectTest, "Bookstore.ProgramA.ObserveRejectAdvancesQueueOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopObserveRejectTest::RunTest(const FString& Parameters)
{
    FFixture F;
    if (!TestTrue(TEXT("Start"), F.Start())) return false;
    const FRunSnapshot Before = F.Snapshot();
    TestFalse(TEXT("Cannot observe a future queue entry"), IShopService::Execute_RequestObserveCustomer(F.Shop.Get(), 1).bSucceeded);
    TestTrue(TEXT("Observe current customer"), IShopService::Execute_RequestObserveCustomer(F.Shop.Get(), 0).bSucceeded);
    TestTrue(TEXT("Observation flag saved"), F.Queue()[0].bObserved);
    TestFalse(TEXT("Observation does not consume the customer"), F.Queue()[0].bServed);
    TestTrue(TEXT("Reject current customer"), IShopService::Execute_RequestRejectCustomer(F.Shop.Get(), 0).bSucceeded);
    TestTrue(TEXT("Rejected customer leaves queue"), F.Queue()[0].bServed);
    TestEqual(TEXT("Resolution records explicit rejection"), static_cast<int32>(F.Queue()[0].Resolution), static_cast<int32>(EShopActionResult::Rejected));
    TestFalse(TEXT("Same customer cannot be rejected twice"), IShopService::Execute_RequestRejectCustomer(F.Shop.Get(), 0).bSucceeded);
    TestFalse(TEXT("Next customer is still available"), F.Queue()[1].bServed);
    TestTrue(TEXT("Queue advanced to customer one"), IShopService::Execute_RequestObserveCustomer(F.Shop.Get(), 1).bSucceeded);
    TestTrue(TEXT("Next observation is independent"), F.Queue()[1].bObserved);
    Unchanged(*this, Before, F.Snapshot());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopFinalMarketTest, "Bookstore.ProgramA.FinalMarketPrecedesEnding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopFinalMarketTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.MaxDays = F.Rules.DaysPerWeek = 7;
    F.Rules.bEnableMarket = true;
    FMarketItemData Item;
    Item.Id = TEXT("TEST_market_enlightenment");
    Item.DisplayName = FText::FromString(TEXT("TEST item, not authored content"));
    Item.Price = 10;
    Item.Week = 1;
    Item.Effect.Add(Resource(EShopEffectType::Enlighten, 60));
    F.Market->AddRow(Item.Id, Item);
    if (!TestTrue(TEXT("Start market fixture"), F.Start())) return false;
    TestFalse(TEXT("Market unavailable before weekly night settlement"), IShopService::Execute_RequestOpenMarket(F.Shop.Get()).bSucceeded);
    for (int32 Day = 1; Day <= 7; ++Day)
    {
        if (!TestTrue(TEXT("Choose night"), F.OpenNight(false)) || !TestTrue(TEXT("Settle night"), F.Settle())) return false;
        if (Day < 7 && !TestTrue(TEXT("Continue nonmarket day"), F.Continue())) return false;
    }
    Phase(*this, F, EGamePhase::NightEnd, TEXT("Final settlement awaits market"));
    TestEqual(TEXT("Ending deferred until final market closes"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::None));
    TestFalse(TEXT("NextDay cannot bypass final market"), IShopService::Execute_RequestNextDay(F.Shop.Get()));
    if (!TestTrue(TEXT("Continue opens weekly market"), F.Continue())) return false;
    Phase(*this, F, EGamePhase::Market, TEXT("Market open"));
    if (!TestTrue(TEXT("Buy test enlightenment"), IShopService::Execute_RequestBuyMarketItem(F.Shop.Get(), Item.Id).bSucceeded)) return false;
    TestEqual(TEXT("Market purchase paid once"), F.Snapshot().Money, 90);
    TestEqual(TEXT("Final-market benefit applies before ending evaluation"), F.Snapshot().Enlighten, 60);
    TestEqual(TEXT("Buying does not prematurely close market"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::None));
    const FRunSnapshot Bought = F.Snapshot();
    TestFalse(TEXT("One-per-run item cannot be bought twice"), IShopService::Execute_RequestBuyMarketItem(F.Shop.Get(), Item.Id).bSucceeded);
    Unchanged(*this, Bought, F.Snapshot());
    TestFalse(TEXT("NextDay unavailable while market open"), IShopService::Execute_RequestNextDay(F.Shop.Get()));
    if (!TestTrue(TEXT("Close final market"), IShopService::Execute_RequestCloseMarket(F.Shop.Get()).bSucceeded)) return false;
    Phase(*this, F, EGamePhase::End, TEXT("Final market closure ends run"));
    TestEqual(TEXT("Purchase changes final ending to Returned"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::Returned));
    TestEqual(TEXT("No extra day created"), F.Snapshot().Day, 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDebtRecoveryTest, "Bookstore.ProgramA.NonnegativeNightResetsDebtStreak", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDebtRecoveryTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.StartMoney = 0;
    F.Rules.Rent = 25;
    F.Books->FindRow<FBookData>(Novel, TEXT("TEST"))->Price = 50;
    if (!TestTrue(TEXT("Start"), F.Start()) || !TestTrue(TEXT("First debt night"), F.OpenNight(false)) || !TestTrue(TEXT("First settlement"), F.Settle())) return false;
    TestEqual(TEXT("Debt streak starts"), F.Snapshot().NegativeDays, 1);
    if (!TestTrue(TEXT("Day two"), F.Continue())) return false;
    if (!TestEqual(TEXT("Open sale"), static_cast<int32>(IShopService::Execute_RequestBeginSell(F.Shop.Get(), 0)), static_cast<int32>(EShopActionResult::Opened))) return false;
    TestEqual(TEXT("Legal sale earns recovery money"), static_cast<int32>(IShopService::Execute_RequestSell(F.Shop.Get(), Novel)), static_cast<int32>(EShopActionResult::Sold));
    if (!TestTrue(TEXT("Recovery night"), F.OpenNight(false)) || !TestTrue(TEXT("Recovery settlement"), F.Settle())) return false;
    TestEqual(TEXT("Balance exactly zero is nonnegative"), F.Snapshot().Money, 0);
    TestEqual(TEXT("Nonnegative night clears consecutive count"), F.Snapshot().NegativeDays, 0);
    if (!TestTrue(TEXT("Day three"), F.Continue()) || !TestTrue(TEXT("Debt returns"), F.OpenNight(false)) || !TestTrue(TEXT("Third settlement"), F.Settle())) return false;
    TestEqual(TEXT("New negative day restarts at one"), F.Snapshot().NegativeDays, 1);
    TestEqual(TEXT("Nonconsecutive debt does not close store"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::None));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPollutionThresholdTest, "Bookstore.ProgramA.PollutionThresholdsAndHeavyGrace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPollutionThresholdTest::RunTest(const FString& Parameters)
{
    struct FThreshold { int32 Value; EPollutionStage Stage; };
    const FThreshold Boundaries[] = {{30, EPollutionStage::Safe}, {31, EPollutionStage::Light}, {60, EPollutionStage::Light}, {61, EPollutionStage::Medium}, {85, EPollutionStage::Medium}, {86, EPollutionStage::Heavy}, {99, EPollutionStage::Heavy}};
    for (const FThreshold& Boundary : Boundaries)
    {
        FFixture F;
        F.Rules.StartPollution = Boundary.Value;
        if (!TestTrue(TEXT("Configure pollution boundary"), F.Start())) return false;
        TestEqual(*FString::Printf(TEXT("Pollution %d maps to expected stage"), Boundary.Value), static_cast<int32>(F.Snapshot().PollutionStage), static_cast<int32>(Boundary.Stage));
    }
    FFixture F;
    F.Rules.StartPollution = 85;
    F.Rules.PollutionOnRead = 5;
    F.Rules.PollutionDecay = 0;
    F.Rules.HeavyGraceTurns = 1;
    F.Rules.HeavyPenalty = 15;
    if (!TestTrue(TEXT("Start heavy fixture"), F.Start())) return false;
    if (F.Snapshot().Phase == EGamePhase::Calm)
        if (!TestTrue(TEXT("Skip initial medium warning"), IShopService::Execute_RequestSkipDecree(F.Shop.Get()).bSucceeded)) return false;
    if (!TestTrue(TEXT("Choose inside"), F.OpenNight(true)) || !TestTrue(TEXT("Read crosses 86"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    TestEqual(TEXT("Read reaches heavy pollution 90"), F.Snapshot().Pollution, 90);
    Phase(*this, F, EGamePhase::Calm, TEXT("Heavy crossing opens calm"));
    TestTrue(TEXT("Player can decline a decree"), IShopService::Execute_RequestSkipDecree(F.Shop.Get()).bSucceeded);
    if (!TestTrue(TEXT("One logical turn completes grace period"), F.Settle())) return false;
    TestEqual(TEXT("Uncontrolled heavy pollution adds 15"), F.Snapshot().Pollution, 105);
    Phase(*this, F, EGamePhase::End, TEXT("Heavy penalty crosses terminal limit"));
    TestEqual(TEXT("Heavy consequence releases pollution"), static_cast<int32>(F.Snapshot().Ending), static_cast<int32>(EShopEnding::PollutionReleased));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopFiniteSecretStockTest, "Bookstore.ProgramA.FiniteCollectReadAndInsufficientPsychic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopFiniteSecretStockTest::RunTest(const FString& Parameters)
{
    FFixture F;
    F.Rules.StartPsychic = 11;
    if (!TestTrue(TEXT("Start"), F.Start()) || !TestTrue(TEXT("Inside"), F.OpenNight(true))) return false;
    const FRunSnapshot Before = F.Snapshot();
    TestFalse(TEXT("Collect cannot spend unavailable psychic"), IShopService::Execute_RequestCollectSecret(F.Shop.Get(), Secret).bSucceeded);
    Unchanged(*this, Before, F.Snapshot());
    TestEqual(TEXT("Failed collect preserves offer"), F.Book(Secret).AvailableToCollect, 1);
    if (!TestTrue(TEXT("Read initial owned copy"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    TestEqual(TEXT("Reading supplies eight psychic"), F.Snapshot().Psychic, 19);
    const FRunSnapshot ReadOnce = F.Snapshot();
    TestFalse(TEXT("Same copy cannot be read twice"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded);
    Unchanged(*this, ReadOnce, F.Snapshot());
    if (!TestTrue(TEXT("Collect after earning enough psychic"), IShopService::Execute_RequestCollectSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    TestEqual(TEXT("Collect deducts twelve psychic"), F.Snapshot().Psychic, 7);
    TestEqual(TEXT("Collect owns another copy"), F.Book(Secret).Stock, 2);
    TestEqual(TEXT("Single nightly offer exhausted"), F.Book(Secret).AvailableToCollect, 0);
    const FRunSnapshot Collected = F.Snapshot();
    TestFalse(TEXT("Exhausted offer cannot be collected again"), IShopService::Execute_RequestCollectSecret(F.Shop.Get(), Secret).bSucceeded);
    Unchanged(*this, Collected, F.Snapshot());
    if (!TestTrue(TEXT("Newly collected physical copy can be read once"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded)) return false;
    TestEqual(TEXT("Both copies now marked read"), F.Book(Secret).ReadCopies, 2);
    TestEqual(TEXT("Only two reading rewards paid"), F.Snapshot().Psychic, 15);
    const FRunSnapshot ReadTwice = F.Snapshot();
    TestFalse(TEXT("Finite owned inventory prevents infinite reading"), IShopService::Execute_RequestReadSecret(F.Shop.Get(), Secret).bSucceeded);
    Unchanged(*this, ReadTwice, F.Snapshot());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCosmeticRandomTest, "Bookstore.ProgramA.OwlDoesNotAffectGameplayRandom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCosmeticRandomTest::RunTest(const FString& Parameters)
{
    FShopCatalog Catalog;
    FOwlLine Line;
    Line.Id = TEXT("TEST_owl"); Line.Text = FText::FromString(TEXT("TEST cosmetic line"));
    Catalog.OwlLines.Add(Line.Id, Line);
    FShopRunState State;
    State.OwlTalkCount = 10;
    State.Random.Initialize(12345);
    State.CosmeticRandom.Initialize(98765);
    const int32 GameplaySeed = State.Random.GetCurrentSeed();
    const int32 CosmeticSeed = State.CosmeticRandom.GetCurrentSeed();
    FName Id; FText Text, Error;
    TestTrue(TEXT("Random cosmetic line resolves"), ShopStory::OwlTalk(State, Catalog, Id, Text, Error));
    TestEqual(TEXT("Gameplay random stream is untouched"), State.Random.GetCurrentSeed(), GameplaySeed);
    TestNotEqual(TEXT("Only cosmetic random stream advances"), State.CosmeticRandom.GetCurrentSeed(), CosmeticSeed);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
