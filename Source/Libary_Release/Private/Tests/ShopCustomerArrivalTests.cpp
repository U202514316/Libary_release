#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopCustomers.h"
#include "ShopEconomy.h"
#include "ShopRunSubsystem.h"
#include "ShopValidation.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace ShopCustomerArrivalTests
{
    const FName Novel(TEXT("TEST_arrival_novel"));
    const FName Poem(TEXT("TEST_arrival_poem"));
    const FName Decree(TEXT("TEST_arrival_calm"));

    FShopCatalog MakeCatalog()
    {
        FShopCatalog C;
        C.Rules.bDaytimeOnlyLoop = true;
        C.Rules.bUseCustomerArrivalDelay = true;
        C.Rules.RandomNeedPool = {EBookType::Novel};
        C.Rules.StartPsychic = 8;
        C.Rules.StartPollution = 5;
        C.Rules.PollutionDecay = 0;
        C.Rules.LightSpreadPerNight = 0;
        C.Rules.bAdvanceTurnOnStageRise = false;
        C.Rules.bAllowEarlyClose = false;
        C.Rules.Rent = 7;
        C.Rules.MaxDays = 2;
        FBookData Book;
        Book.DisplayName = FText::FromString(TEXT("TEST arrival novel"));
        Book.InitialStock = 10; Book.Cost = 2; Book.Price = 20;
        C.Books.Add(Novel, Book);
        Book.BookType = EBookType::Poem;
        C.Books.Add(Poem, Book);
        Book.Layer = EBookLayer::Inside; Book.BookType = EBookType::Secret;
        Book.InitialStock = 1;
        C.Books.Add(TEXT("TEST_arrival_secret"), Book);
        FCustomerData Customer;
        Customer.DisplayName = FText::FromString(TEXT("TEST arrival visitor"));
        Customer.NeedLine = FText::FromString(TEXT("TEST {BookType}"));
        C.Customers.Add(TEXT("TEST_arrival_normal"), Customer);
        Customer.Kind = ECustomerKind::Secret;
        C.Customers.Add(TEXT("TEST_arrival_secret"), Customer);
        FDecreeData Rule;
        Rule.Id = Decree; Rule.DisplayName = FText::FromString(TEXT("TEST arrival calm"));
        Rule.PsychicCost = 8; Rule.PollutionCut = 15;
        C.Decrees.Add(Decree, Rule);
        return C;
    }

    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner{NewObject<UGameInstance>()};
        TStrongObjectPtr<UShopRunSubsystem> Run{NewObject<UShopRunSubsystem>(Owner.Get())};
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        template<typename T> UDataTable* MakeTable()
        {
            UDataTable* Table = NewObject<UDataTable>(Owner.Get());
            Table->RowStruct = T::StaticStruct(); Tables.Emplace(Table); return Table;
        }
        bool Start(const FShopCatalog& C = MakeCatalog())
        {
            UDataTable* Books = MakeTable<FBookData>();
            UDataTable* Customers = MakeTable<FCustomerData>();
            UDataTable* Rules = MakeTable<FRunRules>();
            UDataTable* Decrees = MakeTable<FDecreeData>();
            for (const auto& Pair : C.Books) Books->AddRow(Pair.Key, Pair.Value);
            for (const auto& Pair : C.Customers) Customers->AddRow(Pair.Key, Pair.Value);
            for (const auto& Pair : C.Decrees) Decrees->AddRow(Pair.Key, Pair.Value);
            Rules->AddRow(TEXT("Default"), C.Rules);
            return Run->ConfigureTables(Books, Customers, Rules, Decrees, nullptr, nullptr, nullptr, 731)
                && IShopService::Execute_RequestNewRun(Run.Get());
        }
        ~FFixture() { Run->Deinitialize(); }
        FRunSnapshot Snapshot() const { return IShopService::Execute_GetSnapshot(Run.Get()); }
        TArray<FCustomerRuntime> Queue() const { return IShopService::Execute_GetCustomers(Run.Get()); }
        void Arrive() { Run->Tick(Snapshot().CustomerArrivalRemaining + .01f); }
        bool FinishNight()
        {
            return IShopService::Execute_RequestContinue(Run.Get()) &&
                IShopService::Execute_RequestOpenRestock(Run.Get()).bSucceeded &&
                IShopService::Execute_RequestEndNight(Run.Get()).bSucceeded &&
                IShopService::Execute_RequestContinue(Run.Get());
        }
    };
}

namespace ShopCustomerArrivalTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopArrivalRulesTest, "Bookstore.ProgramA.CustomerArrival.RulesAndRandomIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopArrivalRulesTest::RunTest(const FString&)
{
    FShopCatalog C = MakeCatalog();
    FText Error;
    if (!TestTrue(TEXT("Arrival catalog validates"), ShopValidation::Validate(C, Error))) return false;
    FShopRunState Delayed; ShopEconomy::Reset(Delayed, C);
    Delayed.Day = 1; Delayed.Random.Initialize(731); Delayed.CosmeticRandom.Initialize(907);
    FShopRunState Immediate = Delayed;
    FShopCatalog Legacy = C; Legacy.Rules.bUseCustomerArrivalDelay = false;
    if (!TestTrue(TEXT("Generate delayed queue"), ShopCustomers::GenerateForTime(Delayed, C, false, Error)) ||
        !TestTrue(TEXT("Generate same immediate queue"), ShopCustomers::GenerateForTime(Immediate, Legacy, false, Error))) return false;
    TestEqual(TEXT("Arrival draw preserves business random stream"), Delayed.Random.GetCurrentSeed(), Immediate.Random.GetCurrentSeed());
    TestTrue(TEXT("Delay consumes the cosmetic stream only"), Delayed.CosmeticRandom.GetCurrentSeed() != Immediate.CosmeticRandom.GetCurrentSeed());
    TestFalse(TEXT("First visitor has not arrived"), Delayed.bCustomerPresent);
    TestTrue(TEXT("First wait is between two and four seconds"), Delayed.CustomerArrivalRemaining >= 2.f && Delayed.CustomerArrivalRemaining <= 4.f);
    TestTrue(TEXT("Disabled delay preserves immediate customers"), Immediate.bCustomerPresent);
    TestEqual(TEXT("Disabled delay has no remaining wait"), Immediate.CustomerArrivalRemaining, 0.f);
    for (int32 Index = 0; Index < Delayed.Customers.Num(); ++Index)
    {
        TestEqual(TEXT("Arrival timing cannot alter the customer draw"), Delayed.Customers[Index].TemplateId, Immediate.Customers[Index].TemplateId);
        TestEqual(TEXT("Arrival timing cannot alter the need draw"), Delayed.Customers[Index].NeedType, Immediate.Customers[Index].NeedType);
    }
    const float Patience = Delayed.Customers[0].Patience;
    TestEqual(TEXT("Pure patience helper rejects waiting visitor time"), ShopCustomers::AdvancePatience(Delayed, C.Rules, 1000.f), INDEX_NONE);
    TestEqual(TEXT("Waiting pure model retains full patience"), Delayed.Customers[0].Patience, Patience);
    for (int32 Case = 0; Case < 3; ++Case)
    {
        FShopCatalog Invalid = C;
        if (Case == 0) Invalid.Rules.CustomerArrivalMin = -1.f;
        if (Case == 1) Invalid.Rules.CustomerArrivalMax = 1.f;
        if (Case == 2) Invalid.Rules.CustomerArrivalMax = std::numeric_limits<float>::infinity();
        TestFalse(TEXT("Invalid arrival range rejected by catalog validation"), ShopValidation::Validate(Invalid, Error));
        const int32 BusinessSeed = Delayed.Random.GetCurrentSeed();
        const int32 CosmeticSeed = Delayed.CosmeticRandom.GetCurrentSeed();
        const float Remaining = Delayed.CustomerArrivalRemaining;
        TestFalse(TEXT("Pure generation rejects invalid arrival range"), ShopCustomers::GenerateForTime(Delayed, Invalid, false, Error));
        TestEqual(TEXT("Rejected range preserves business stream"), Delayed.Random.GetCurrentSeed(), BusinessSeed);
        TestEqual(TEXT("Rejected range preserves cosmetic stream"), Delayed.CosmeticRandom.GetCurrentSeed(), CosmeticSeed);
        TestEqual(TEXT("Rejected range preserves existing wait"), Delayed.CustomerArrivalRemaining, Remaining);
    }
    C.Rules.bDaytimeOnlyLoop = false;
    if (!TestTrue(TEXT("Legacy mode with new flag still generates"), ShopCustomers::GenerateForTime(Delayed, C, false, Error))) return false;
    TestTrue(TEXT("Arrival feature never changes the legacy flow"), Delayed.bCustomerPresent);
    TestEqual(TEXT("Legacy flow has no arrival wait"), Delayed.CustomerArrivalRemaining, 0.f);
    C.Rules.bDaytimeOnlyLoop = true;
    C.Rules.CustomerArrivalMin = C.Rules.CustomerArrivalMax = 0.f;
    if (!TestTrue(TEXT("Explicit zero delay is supported"), ShopCustomers::GenerateForTime(Delayed, C, false, Error))) return false;
    TestTrue(TEXT("Zero delay immediately presents a customer"), Delayed.bCustomerPresent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopArrivalLifecycleTest, "Bookstore.ProgramA.CustomerArrival.ServiceLifecycleAndRestart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopArrivalLifecycleTest::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Start arrival service fixture"), F.Start())) return false;
    const float FirstDelay = F.Snapshot().CustomerArrivalRemaining;
    const float FullPatience = F.Queue()[0].MaxPatience;
    TestFalse(TEXT("Day opens with an empty counter"), F.Run->IsCurrentCustomerPresent());
    TestFalse(TEXT("Snapshot also reports absent customer"), F.Snapshot().bCustomerPresent);
    TestEqual(TEXT("No active sale while waiting"), F.Run->GetActiveCustomerIndex(), INDEX_NONE);
    TestEqual(TEXT("Cannot start selling before arrival"), IShopService::Execute_RequestBeginSell(F.Run.Get(), 0), EShopActionResult::Unavailable);
    TestEqual(TEXT("Cannot observe before arrival"), IShopService::Execute_RequestObserveCustomer(F.Run.Get(), 0).Code, EShopActionResult::Unavailable);
    TestEqual(TEXT("Cannot reject before arrival"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), 0).Code, EShopActionResult::Unavailable);
    TestFalse(TEXT("Early commands never reveal observation"), F.Queue()[0].bObserved);
    TestFalse(TEXT("Early commands never consume a visitor"), F.Queue()[0].bServed);
    TestEqual(TEXT("Early commands do not redraw arrival time"), F.Snapshot().CustomerArrivalRemaining, FirstDelay);
    F.Run->Tick(.5f);
    TestEqual(TEXT("Only the wait counts down"), F.Snapshot().CustomerArrivalRemaining, FirstDelay - .5f);
    TestEqual(TEXT("Waiting does not secretly reduce patience"), F.Queue()[0].Patience, FullPatience);
    F.Arrive();
    TestTrue(TEXT("Arrival appears through the service query"), F.Run->IsCurrentCustomerPresent());
    TestTrue(TEXT("Arrival appears through the snapshot"), F.Snapshot().bCustomerPresent);
    TestEqual(TEXT("Arrival frame keeps full patience including overshoot"), F.Queue()[0].Patience, FullPatience);
    TestEqual(TEXT("Presence is not an open sale"), F.Run->GetActiveCustomerIndex(), INDEX_NONE);
    F.Run->Tick(.5f);
    TestEqual(TEXT("Patience starts on the next frame"), F.Queue()[0].Patience, FullPatience - .5f);
    if (!TestTrue(TEXT("Present visitor can be observed"), IShopService::Execute_RequestObserveCustomer(F.Run.Get(), 0).bSucceeded) ||
        !TestEqual(TEXT("Present visitor can open selling"), IShopService::Execute_RequestBeginSell(F.Run.Get(), 0), EShopActionResult::Opened)) return false;
    TestEqual(TEXT("Active sale is the selected current visitor"), F.Run->GetActiveCustomerIndex(), 0);
    if (!TestTrue(TEXT("Cancel only closes book selection"), IShopService::Execute_RequestCancelSell(F.Run.Get()))) return false;
    TestTrue(TEXT("Cancel does not make the visitor arrive again"), F.Run->IsCurrentCustomerPresent());
    TestEqual(TEXT("Cancel never grants another wait"), F.Snapshot().CustomerArrivalRemaining, 0.f);
    if (!TestEqual(TEXT("Reopen first visitor"), IShopService::Execute_RequestBeginSell(F.Run.Get(), 0), EShopActionResult::Opened) ||
        !TestEqual(TEXT("Sell first visitor"), IShopService::Execute_RequestSell(F.Run.Get(), Novel), EShopActionResult::Sold)) return false;
    TestFalse(TEXT("Second visitor waits after sale"), F.Run->IsCurrentCustomerPresent());
    TestTrue(TEXT("Second wait is independently within configured bounds"), F.Snapshot().CustomerArrivalRemaining >= 2.f && F.Snapshot().CustomerArrivalRemaining <= 4.f);
    TestEqual(TEXT("Waiting second visitor cannot be rejected"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), 1).Code, EShopActionResult::Unavailable);
    F.Arrive();
    if (!TestTrue(TEXT("Reject arrived second visitor"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), 1).bSucceeded)) return false;
    TestFalse(TEXT("Third visitor waits after refusal"), F.Run->IsCurrentCustomerPresent());
    F.Arrive();
    F.Run->Tick(F.Queue()[2].Patience + 1.f);
    TestEqual(TEXT("Last expiry moves immediately to DayEnd"), F.Snapshot().Phase, EGamePhase::DayEnd);
    TestEqual(TEXT("Last visitor result survives for callbacks"), F.Queue()[2].Resolution, EShopActionResult::Expired);
    TestFalse(TEXT("No fourth visitor is scheduled"), F.Snapshot().bCustomerPresent);
    TestEqual(TEXT("No delay remains after the final visitor"), F.Snapshot().CustomerArrivalRemaining, 0.f);
    TestEqual(TEXT("Daily rent is charged once"), F.Snapshot().Money, 100 + 20 - 7);
    F.Run->Tick(500.f);
    TestEqual(TEXT("Idle DayEnd cannot charge rent again"), F.Snapshot().Money, 113);
    if (!TestTrue(TEXT("Complete night and advance day"), F.FinishNight())) return false;
    TestEqual(TEXT("New day has fresh three-slot queue"), F.Queue().Num(), 3);
    TestFalse(TEXT("Next day starts with another first-arrival delay"), F.Snapshot().bCustomerPresent);
    TestEqual(TEXT("Next day's first visitor has full patience"), F.Queue()[0].Patience, F.Queue()[0].MaxPatience);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        F.Arrive();
        if (!TestTrue(TEXT("Resolve each arrived final-day slot"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), Index).bSucceeded)) return false;
    }
    if (!TestTrue(TEXT("Final night still reaches the configured ending"), F.FinishNight())) return false;
    TestEqual(TEXT("Arrival waits do not add days to the run"), F.Snapshot().Phase, EGamePhase::End);
    if (!TestTrue(TEXT("Restart bounded run"), IShopService::Execute_RequestNewRun(F.Run.Get()))) return false;
    TestEqual(TEXT("Restart resets day"), F.Snapshot().Day, 1);
    TestEqual(TEXT("Restart resets the seeded first arrival"), F.Snapshot().CustomerArrivalRemaining, FirstDelay);
    TestFalse(TEXT("Restart never inherits the prior present visitor"), F.Snapshot().bCustomerPresent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopArrivalModalTest, "Bookstore.ProgramA.CustomerArrival.ModalAndCompletionPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopArrivalModalTest::RunTest(const FString&)
{
    FShopCatalog C = MakeCatalog();
    C.Rules.bUniqueDailyCustomerPortraits = true;
    C.Rules.StartPollution = C.Rules.LightThreshold;
    FShopEffect Fake; Fake.Type = EShopEffectType::SpawnFakeCustomer; Fake.Amount = 2;
    C.Decrees[Decree].Effects.Add(Fake);
    FFixture F;
    if (!TestTrue(TEXT("Start in Calm before the first arrival"), F.Start(C))) return false;
    const float Remaining = F.Snapshot().CustomerArrivalRemaining;
    TestEqual(TEXT("Initial pollution opens Calm"), F.Snapshot().Phase, EGamePhase::Calm);
    F.Run->Tick(1000.f);
    TestEqual(TEXT("Calm freezes arrival timer"), F.Snapshot().CustomerArrivalRemaining, Remaining);
    TestEqual(TEXT("Calm freezes customer patience"), F.Queue()[0].Patience, F.Queue()[0].MaxPatience);
    const int32 HeadPortrait = F.Queue()[0].PortraitSlot;
    if (!TestTrue(TEXT("Enact decree and restore waiting daytime queue"), IShopService::Execute_RequestEnactDecree(F.Run.Get(), Decree).bSucceeded)) return false;
    TestEqual(TEXT("Modal recovery resumes Day"), F.Snapshot().Phase, EGamePhase::Day);
    TestEqual(TEXT("Modal recovery preserves the exact remaining wait"), F.Snapshot().CustomerArrivalRemaining, Remaining);
    TestTrue(TEXT("Fake visitors occupy only waiting slots"), !F.Queue()[0].bFake && F.Queue()[1].bFake && F.Queue()[2].bFake);
    TestEqual(TEXT("Disguises do not replace the current customer's portrait"), F.Queue()[0].PortraitSlot, HeadPortrait);
    TSet<int32> Disguises;
    for (const auto& Visitor : F.Queue())
    {
        TestTrue(TEXT("Fake visitors retain ordinary appearances"), Visitor.PortraitSlot >= 0 && Visitor.PortraitSlot < 4);
        TestFalse(TEXT("Fake replacements cannot duplicate today's portraits"), Disguises.Contains(Visitor.PortraitSlot));
        Disguises.Add(Visitor.PortraitSlot);
    }
    F.Arrive();
    if (!TestTrue(TEXT("Resolve ordinary head before fakes"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), 0).bSucceeded)) return false;
    const int32 Pollution = F.Snapshot().Pollution;
    F.Run->Tick(.5f);
    TestEqual(TEXT("Absent fake cannot apply pollution"), F.Snapshot().Pollution, Pollution);
    TestEqual(TEXT("Absent fake cannot age its pollution clock"), F.Queue()[1].FakePollutionElapsed, 0.f);
    TestEqual(TEXT("Absent fake retains full patience"), F.Queue()[1].Patience, F.Queue()[1].MaxPatience);
    F.Arrive();
    TestEqual(TEXT("Fake arrival frame cannot apply pollution"), F.Snapshot().Pollution, Pollution);
    F.Run->Tick(1.f);
    TestEqual(TEXT("Arrived fake applies configured per-second pollution"), F.Snapshot().Pollution, Pollution + C.Rules.FakeCustomerPollutionPerSecond);
    if (!TestTrue(TEXT("Arrived fake can be observed"), IShopService::Execute_RequestObserveCustomer(F.Run.Get(), 1).bSucceeded) ||
        !TestTrue(TEXT("Arrived fake can be refused"), IShopService::Execute_RequestRejectCustomer(F.Run.Get(), 1).bSucceeded)) return false;
    TestFalse(TEXT("Refusing fake schedules last arrival"), F.Snapshot().bCustomerPresent);

    for (int32 Case = 0; Case < 2; ++Case)
    {
        FShopCatalog OutcomeCatalog = MakeCatalog();
        if (Case == 1) OutcomeCatalog.Books[Novel].InitialStock = 0;
        FFixture Outcome;
        if (!TestTrue(TEXT("Start completion-outcome fixture"), Outcome.Start(OutcomeCatalog))) return false;
        Outcome.Arrive();
        if (Case == 0)
        {
            if (!TestEqual(TEXT("Open wrong-book transaction"), IShopService::Execute_RequestBeginSell(Outcome.Run.Get(), 0), EShopActionResult::Opened)) return false;
            TestEqual(TEXT("Wrong book consumes visitor under authored rule"), IShopService::Execute_RequestSell(Outcome.Run.Get(), Poem), EShopActionResult::WrongBook);
        }
        else TestEqual(TEXT("No-match consumes visitor under authored rule"), IShopService::Execute_RequestBeginSell(Outcome.Run.Get(), 0), EShopActionResult::NoMatch);
        TestTrue(TEXT("Outcome leaves a resolved first visitor"), Outcome.Queue()[0].bServed);
        TestFalse(TEXT("Both completion paths wait before the next visitor"), Outcome.Snapshot().bCustomerPresent);
        TestTrue(TEXT("Both completion paths schedule a bounded delay"), Outcome.Snapshot().CustomerArrivalRemaining >= 2.f && Outcome.Snapshot().CustomerArrivalRemaining <= 4.f);
        TestEqual(TEXT("Outcome cannot lower the next visitor's patience"), Outcome.Queue()[1].Patience, Outcome.Queue()[1].MaxPatience);
    }
    return true;
}

} // namespace ShopCustomerArrivalTests

#endif // WITH_DEV_AUTOMATION_TESTS
