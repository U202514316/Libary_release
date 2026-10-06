#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace ShopTableShopFlowTests
{
    const FName Novel(TEXT("TEST_front_novel"));
    const FName Secret(TEXT("TEST_stored_secret"));
    const FName Decree(TEXT("TEST_nightly_pollution"));

    // Native fixtures deliberately exercise the Blueprint-facing service commands.
    // These rows are deterministic test content, never authored release data.
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner{NewObject<UGameInstance>()};
        TStrongObjectPtr<UShopRunSubsystem> Run{NewObject<UShopRunSubsystem>(Owner.Get())};
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        FRunRules Rules;
        FDecreeData Rule;

        template<typename T> UDataTable* MakeTable()
        {
            UDataTable* Table = NewObject<UDataTable>(Owner.Get());
            Table->RowStruct = T::StaticStruct(); Tables.Emplace(Table); return Table;
        }

        FFixture()
        {
            UDataTable* Books = MakeTable<FBookData>();
            UDataTable* Customers = MakeTable<FCustomerData>();
            MakeTable<FRunRules>(); MakeTable<FDecreeData>(); MakeTable<FEventData>();
            MakeTable<FMarketItemData>(); MakeTable<FOwlLine>();
            FBookData Book;
            Book.DisplayName = FText::FromString(TEXT("TEST ordinary product"));
            Book.InitialStock = 10; Book.Price = 20;
            Books->AddRow(Novel, Book);
            Book.DisplayName = FText::FromString(TEXT("TEST secret product"));
            Book.Layer = EBookLayer::Inside; Book.BookType = EBookType::Secret;
            Book.InitialStock = 2; Book.Price = 60; Book.CollectOfferPerNight = 0;
            Books->AddRow(Secret, Book);
            FCustomerData Customer;
            Customer.DisplayName = FText::FromString(TEXT("TEST normal visitor"));
            Customer.NeedLine = FText::FromString(TEXT("TEST {类型}"));
            Customers->AddRow(TEXT("TEST_normal"), Customer);
            Customer.DisplayName = FText::FromString(TEXT("TEST secret visitor"));
            Customer.Kind = ECustomerKind::Secret;
            Customers->AddRow(TEXT("TEST_secret"), Customer);
            Rules.Rent = 0; Rules.StartPsychic = 0;
            Rules.CustomersMin = Rules.CustomersMax = 1;
            Rules.InsideCustomers = 16; Rules.WeekTwoCustomerBonus = 0;
            Rules.RandomNeedPool = { EBookType::Novel };
            Rules.PollutionDecay = 0; Rules.LightSpreadPerNight = 0;
            Rules.bNoMatchConsumesCustomer = false; Rules.bWrongBookConsumesCustomer = false;
            Rules.bEnableHistory = false; Rules.bEnableMarket = false;
            Rule.Id = Decree; Rule.DisplayName = FText::FromString(TEXT("TEST pollution source"));
            Rule.PsychicCost = 0; Rule.PollutionCut = 0; Rule.LoopholeDelay = 2;
        }
        ~FFixture() { Run->Deinitialize(); }
        bool Start()
        {
            Tables[2]->AddRow(TEXT("Default"), Rules); Tables[3]->AddRow(Decree, Rule);
            return Run->ConfigureTables(Tables[0].Get(), Tables[1].Get(), Tables[2].Get(), Tables[3].Get(),
                Tables[4].Get(), Tables[5].Get(), Tables[6].Get(), 731) && IShopService::Execute_RequestNewRun(Run.Get());
        }
        FRunSnapshot Snapshot() const { return IShopService::Execute_GetSnapshot(Run.Get()); }
        TArray<FCustomerRuntime> Queue() const { return IShopService::Execute_GetCustomers(Run.Get()); }
        FBookRuntime Book(FName Id = Secret) const { FBookRuntime Out; Run->GetBookRuntime(Id, Out); return Out; }
        bool Inside()
        {
            return IShopService::Execute_RequestEndDay(Run.Get()) && IShopService::Execute_RequestContinue(Run.Get()) &&
                IShopService::Execute_RequestOpenInside(Run.Get()).bSucceeded;
        }
        bool Front() { return IShopService::Execute_RequestOpenTableShop(Run.Get()).bSucceeded; }
        bool List() { return IShopService::Execute_RequestListSecretBook(Run.Get(), Secret).bSucceeded; }
        int32 AdvanceTo(ECustomerKind Kind)
        {
            const auto Customers = Queue();
            int32 Target = INDEX_NONE;
            for (int32 Index = 0; Index < Customers.Num(); ++Index)
                if (!Customers[Index].bServed && Customers[Index].Kind == Kind) { Target = Index; break; }
            if (Target == INDEX_NONE) return INDEX_NONE;
            for (int32 Index = 0; Index < Target; ++Index)
                if (!Customers[Index].bServed && !IShopService::Execute_RequestRejectCustomer(Run.Get(), Index).bSucceeded) return INDEX_NONE;
            return Target;
        }
    };

    void SameResources(FAutomationTestBase& Test, const FRunSnapshot& A, const FRunSnapshot& B)
    {
        Test.TestEqual(TEXT("Money is unchanged"), B.Money, A.Money);
        Test.TestEqual(TEXT("Psychic is unchanged"), B.Psychic, A.Psychic);
        Test.TestEqual(TEXT("Pollution is unchanged"), B.Pollution, A.Pollution);
        Test.TestEqual(TEXT("Total owned inventory is unchanged"), B.TotalStock, A.TotalStock);
        Test.TestEqual(TEXT("Daily income is unchanged"), B.TodayIncome, A.TodayIncome);
        Test.TestEqual(TEXT("Daily expense is unchanged"), B.TodayExpense, A.TodayExpense);
        Test.TestEqual(TEXT("Sales count is unchanged"), B.TotalSold, A.TotalSold);
    }

    void SameQueue(FAutomationTestBase& Test, const TArray<FCustomerRuntime>& A, const TArray<FCustomerRuntime>& B)
    {
        if (!Test.TestEqual(TEXT("Returning preserves the number of customers"), B.Num(), A.Num())) return;
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            Test.TestEqual(TEXT("Customer identity is preserved"), B[Index].TemplateId, A[Index].TemplateId);
            Test.TestEqual(TEXT("Customer role is preserved"), B[Index].Kind, A[Index].Kind);
            Test.TestEqual(TEXT("Product provenance is preserved"), B[Index].NeedLayer, A[Index].NeedLayer);
            Test.TestEqual(TEXT("Product demand is preserved"), B[Index].NeedType, A[Index].NeedType);
            Test.TestEqual(TEXT("Patience is preserved"), B[Index].Patience, A[Index].Patience);
            Test.TestEqual(TEXT("Served state is preserved"), B[Index].bServed, A[Index].bServed);
        }
    }
}

using namespace ShopTableShopFlowTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopListingConservationTest, "Bookstore.ProgramA.TableShop.ListingConservationAndNewRun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopListingConservationTest::RunTest(const FString&)
{
    FFixture F; F.Rules.MaxDays = 1;
    if (!TestTrue(TEXT("Start listing scenario"), F.Start())) return false;
    TestEqual(TEXT("Initial owned total"), F.Book().Stock, 2);
    TestEqual(TEXT("Initial listed count is zero"), F.Book().ListedCopies, 0);
    TestEqual(TEXT("Initial stored count equals owned total"), F.Book().StoredCopies, 2);
    const auto BeforeDay = F.Snapshot();
    TestFalse(TEXT("Listing is unavailable during daytime"), F.List());
    SameResources(*this, BeforeDay, F.Snapshot());
    if (!TestTrue(TEXT("Enter management"), F.Inside())) return false;
    TestEqual(TEXT("Management creates no customer queue"), F.Queue().Num(), 0);
    TestTrue(TEXT("Management cannot begin a sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), 0) != EShopActionResult::Opened);
    const auto Before = F.Snapshot();
    TestTrue(TEXT("List one copy"), F.List());
    TestEqual(TEXT("Listing preserves total stock"), F.Book().Stock, 2);
    TestEqual(TEXT("Exactly one copy becomes listed"), F.Book().ListedCopies, 1);
    TestEqual(TEXT("Exactly one copy remains stored"), F.Book().StoredCopies, 1);
    TestTrue(TEXT("Listing preserves seal"), F.Book().SecretCopies[0].bSealed);
    TestFalse(TEXT("Listing does not mark a copy read"), F.Book().SecretCopies[0].bRead);
    SameResources(*this, Before, F.Snapshot());
    TestTrue(TEXT("List second copy"), F.List());
    TestFalse(TEXT("Listing cannot manufacture a third copy"), F.List());
    TestEqual(TEXT("All copies listed"), F.Book().ListedCopies, 2);
    TestEqual(TEXT("No copies remain stored"), F.Book().StoredCopies, 0);
    TestTrue(TEXT("Unlist exactly one copy"), IShopService::Execute_RequestUnlistSecretBook(F.Run.Get(), Secret).bSucceeded);
    TestEqual(TEXT("Unlisting preserves stock"), F.Book().Stock, 2);
    TestEqual(TEXT("Unlisting leaves one listed copy"), F.Book().ListedCopies, 1);
    TestEqual(TEXT("Unlisting returns one stored copy"), F.Book().StoredCopies, 1);
    TestFalse(TEXT("Ordinary book cannot use secret listing command"), IShopService::Execute_RequestListSecretBook(F.Run.Get(), Novel).bSucceeded);
    TestFalse(TEXT("Unknown book cannot be listed"), IShopService::Execute_RequestListSecretBook(F.Run.Get(), TEXT("TEST_missing")).bSucceeded);
    SameResources(*this, Before, F.Snapshot());
    if (!TestTrue(TEXT("Finish the single test day"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded)) return false;
    if (!TestTrue(TEXT("Continue to ending"), IShopService::Execute_RequestContinue(F.Run.Get()))) return false;
    TestEqual(TEXT("The test run has ended"), F.Snapshot().Phase, EGamePhase::End);
    if (!TestTrue(TEXT("Restart after ending"), IShopService::Execute_RequestNewRun(F.Run.Get()))) return false;
    TestEqual(TEXT("New run restores stock"), F.Book().Stock, 2);
    TestEqual(TEXT("New run clears prior listings"), F.Book().ListedCopies, 0);
    TestEqual(TEXT("New run stores every initial copy"), F.Book().StoredCopies, 2);
    for (const auto& Copy : F.Book().SecretCopies) TestFalse(TEXT("Every fresh copy is unlisted"), Copy.bListedForSale);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightRolesTest, "Bookstore.ProgramA.TableShop.NightRolesAndListedSales", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightRolesTest::RunTest(const FString&)
{
    {
        FFixture F;
        if (!TestTrue(TEXT("Start secret visitor scenario"), F.Start()) || !TestTrue(TEXT("Inside"), F.Inside()) || !TestTrue(TEXT("Night storefront"), F.Front())) return false;
        const int32 Index = F.AdvanceTo(ECustomerKind::Secret);
        if (!TestTrue(TEXT("Deterministic night queue contains a Secret visitor"), Index != INDEX_NONE)) return false;
        const auto Before = F.Snapshot();
        TestEqual(TEXT("Owned but unlisted secret books cannot satisfy demand"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::NoMatch);
        TestFalse(TEXT("Configured no-match policy preserves visitor"), F.Queue()[Index].bServed);
        SameResources(*this, Before, F.Snapshot());
        if (!TestTrue(TEXT("Return to management"), IShopService::Execute_RequestOpenInside(F.Run.Get()).bSucceeded) || !TestTrue(TEXT("List one copy"), F.List()) || !TestTrue(TEXT("Resume storefront"), F.Front())) return false;
        TestEqual(TEXT("Listed stock opens sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened);
        TestEqual(TEXT("New sale phase is NightSell"), F.Snapshot().Phase, EGamePhase::NightSell);
        TestEqual(TEXT("Secret visitor rejects ordinary product"), IShopService::Execute_RequestSell(F.Run.Get(), Novel), EShopActionResult::WrongBook);
        SameResources(*this, Before, F.Snapshot());
        TestEqual(TEXT("Listed secret sells"), IShopService::Execute_RequestSell(F.Run.Get(), Secret), EShopActionResult::Sold);
        TestEqual(TEXT("Sale returns to night storefront"), F.Snapshot().Phase, EGamePhase::NightShop);
        TestEqual(TEXT("One price is earned"), F.Snapshot().Money, Before.Money + 60);
        TestEqual(TEXT("One sale adds configured pollution"), F.Snapshot().Pollution, Before.Pollution + F.Rules.PollutionOnSell);
        TestEqual(TEXT("Only one owned copy is removed"), F.Book().Stock, 1);
        TestEqual(TEXT("The sold listed copy is removed"), F.Book().ListedCopies, 0);
        TestEqual(TEXT("Stored copy is untouched"), F.Book().StoredCopies, 1);
        TestTrue(TEXT("Customer is resolved once"), F.Queue()[Index].bServed);
        const auto Sold = F.Snapshot();
        TestTrue(TEXT("Duplicate sale cannot succeed"), IShopService::Execute_RequestSell(F.Run.Get(), Secret) != EShopActionResult::Sold);
        SameResources(*this, Sold, F.Snapshot());
    }
    {
        FFixture F;
        if (!TestTrue(TEXT("Start ordinary visitor scenario"), F.Start()) || !TestTrue(TEXT("Inside"), F.Inside()) || !TestTrue(TEXT("List"), F.List()) || !TestTrue(TEXT("Night storefront"), F.Front())) return false;
        const int32 Index = F.AdvanceTo(ECustomerKind::Normal);
        if (!TestTrue(TEXT("Deterministic queue contains a Normal visitor"), Index != INDEX_NONE)) return false;
        TestEqual(TEXT("Normal visitor opens an ordinary sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened);
        const auto Before = F.Snapshot();
        TestEqual(TEXT("Normal visitor cannot buy a listed secret book"), IShopService::Execute_RequestSell(F.Run.Get(), Secret), EShopActionResult::WrongBook);
        SameResources(*this, Before, F.Snapshot());
        TestEqual(TEXT("Wrong-book rejection preserves listed copy"), F.Book().ListedCopies, 1);
        TestFalse(TEXT("Open sale blocks management transition"), IShopService::Execute_RequestOpenInside(F.Run.Get()).bSucceeded);
        TestFalse(TEXT("Open sale blocks night settlement"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded);
        TestTrue(TEXT("Cancel closes night sale"), IShopService::Execute_RequestCancelSell(F.Run.Get()));
        TestEqual(TEXT("Cancel returns to night storefront"), F.Snapshot().Phase, EGamePhase::NightShop);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightSaleAtomicTest, "Bookstore.ProgramA.TableShop.RejectedSalesAreAtomic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightSaleAtomicTest::RunTest(const FString&)
{
    for (const bool bMoneyOverflow : { true, false })
    {
        FFixture F;
        if (bMoneyOverflow) F.Rules.StartMoney = MAX_int32 - 10;
        else { F.Rules.StartPollution = 1; F.Rules.PollutionOnSell = MAX_int32; }
        if (!TestTrue(TEXT("Start overflow scenario"), F.Start()) || !TestTrue(TEXT("Inside"), F.Inside()) || !TestTrue(TEXT("List"), F.List()) || !TestTrue(TEXT("Night storefront"), F.Front())) return false;
        const int32 Index = F.AdvanceTo(ECustomerKind::Secret);
        if (!TestTrue(TEXT("Secret visitor exists"), Index != INDEX_NONE)) return false;
        if (!TestEqual(TEXT("Begin sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened)) return false;
        const auto Before = F.Snapshot(); const auto BookBefore = F.Book(); const auto QueueBefore = F.Queue();
        TestEqual(TEXT("Overflow rejects the entire transaction"), IShopService::Execute_RequestSell(F.Run.Get(), Secret), EShopActionResult::Rejected);
        SameResources(*this, Before, F.Snapshot()); SameQueue(*this, QueueBefore, F.Queue());
        TestEqual(TEXT("Failure stays in the sale overlay"), F.Snapshot().Phase, EGamePhase::NightSell);
        TestEqual(TEXT("Failure preserves physical copies"), F.Book().SecretCopies.Num(), BookBefore.SecretCopies.Num());
        TestEqual(TEXT("Failure preserves listed count"), F.Book().ListedCopies, BookBefore.ListedCopies);
        TestEqual(TEXT("Failure preserves stored count"), F.Book().StoredCopies, BookBefore.StoredCopies);
        TestEqual(TEXT("Failure does not trigger an ending"), F.Snapshot().Ending, EShopEnding::None);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightQueuePauseTest, "Bookstore.ProgramA.TableShop.NightQueuePersistsAndPauses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightQueuePauseTest::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Start queue scenario"), F.Start()) || !TestTrue(TEXT("Inside"), F.Inside()) || !TestTrue(TEXT("List"), F.List()) || !TestTrue(TEXT("Night storefront"), F.Front())) return false;
    TestEqual(TEXT("Exactly one configured night queue"), F.Queue().Num(), F.Rules.InsideCustomers);
    const float InitialPatience = F.Queue()[0].Patience;
    F.Run->Tick(2.f);
    TestEqual(TEXT("Nighttime head loses patience"), F.Queue()[0].Patience, InitialPatience - 2.f);
    const auto Before = F.Queue();
    for (int32 Trip = 0; Trip < 3; ++Trip)
    {
        if (!TestTrue(TEXT("Return to management"), IShopService::Execute_RequestOpenInside(F.Run.Get()).bSucceeded)) return false;
        F.Run->Tick(50.f);
        SameQueue(*this, Before, F.Queue());
        if (!TestTrue(TEXT("Resume the same night"), F.Front())) return false;
        SameQueue(*this, Before, F.Queue());
        TestFalse(TEXT("A different dusk choice remains unavailable"), IShopService::Execute_RequestOpenRestock(F.Run.Get()).bSucceeded);
    }
    F.Run->Tick(1.f);
    TestEqual(TEXT("Returning resumes rather than resets patience"), F.Queue()[0].Patience, InitialPatience - 3.f);
    if (!TestEqual(TEXT("First visitor can open the appropriate sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), 0), EShopActionResult::Opened)) return false;
    TestFalse(TEXT("NightSell cannot end night"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded);
    TestTrue(TEXT("Cancel before ending night"), IShopService::Execute_RequestCancelSell(F.Run.Get()));
    TestTrue(TEXT("NightShop may end the night directly"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded);
    TestEqual(TEXT("NightShop settlement shows NightEnd"), F.Snapshot().Phase, EGamePhase::NightEnd);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightTerminalPollutionTest, "Bookstore.ProgramA.TableShop.NightPollutionLimitSurvivesDecay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightTerminalPollutionTest::RunTest(const FString&)
{
    FFixture F;
    F.Rules.StartPollution = 99; F.Rules.PollutionDecay = 3; F.Rules.HeavyGraceTurns = 100;
    FShopEffect Effect; Effect.Type = EShopEffectType::NightlyPollution; Effect.Amount = 1;
    F.Rule.Effects.Add(Effect);
    if (!TestTrue(TEXT("Start near pollution limit without reading"), F.Start())) return false;
    TestEqual(TEXT("High initial pollution opens calm"), F.Snapshot().Phase, EGamePhase::Calm);
    if (!TestTrue(TEXT("Install a configured nightly pollution source"), IShopService::Execute_RequestEnactDecree(F.Run.Get(), Decree).bSucceeded)) return false;
    if (!TestTrue(TEXT("Enter management"), F.Inside())) return false;
    if (!TestTrue(TEXT("Settle once: 99 + 1 - 3"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded)) return false;
    TestEqual(TEXT("Decay still lowers displayed pollution"), F.Snapshot().Pollution, 97);
    TestEqual(TEXT("Touching 100 before decay remains terminal"), F.Snapshot().Ending, EShopEnding::PollutionReleased);
    TestEqual(TEXT("Terminal ending survives settlement and modal processing"), F.Snapshot().Phase, EGamePhase::End);
    TestEqual(TEXT("No psychic or reading was needed for this terminal path"), F.Snapshot().Psychic, 0);
    const auto Ended = F.Snapshot();
    TestFalse(TEXT("Cannot reopen storefront after terminal ending"), F.Front());
    TestFalse(TEXT("Cannot settle terminal night twice"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded);
    SameResources(*this, Ended, F.Snapshot());
    if (!TestTrue(TEXT("New run clears terminal state"), IShopService::Execute_RequestNewRun(F.Run.Get()))) return false;
    TestEqual(TEXT("New run restores configured high pollution"), F.Snapshot().Pollution, 99);
    TestEqual(TEXT("Sticky limit flag is cleared for new run"), F.Snapshot().Ending, EShopEnding::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightSaleThresholdTest, "Bookstore.ProgramA.TableShop.SecretSaleCalmAndTerminalTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightSaleThresholdTest::RunTest(const FString&)
{
    for (const int32 InitialPollution : { 25, 95 })
    {
        FFixture F;
        F.Rules.StartPollution = InitialPollution;
        if (!TestTrue(TEXT("Start secret sale pollution scenario"), F.Start())) return false;
        if (InitialPollution == 95)
        {
            TestEqual(TEXT("Initial heavy pollution opens Calm"), F.Snapshot().Phase, EGamePhase::Calm);
            if (!TestTrue(TEXT("Skip initial Calm without enacting a decree"), IShopService::Execute_RequestSkipDecree(F.Run.Get()).bSucceeded)) return false;
        }
        if (!TestTrue(TEXT("Inside management"), F.Inside()) || !TestTrue(TEXT("List secret copy"), F.List()) || !TestTrue(TEXT("Open night storefront"), F.Front())) return false;
        const int32 Index = F.AdvanceTo(ECustomerKind::Secret);
        if (!TestTrue(TEXT("Night queue contains a Secret visitor"), Index != INDEX_NONE)) return false;
        if (!TestEqual(TEXT("Begin listed secret sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened)) return false;
        const auto Before = F.Snapshot();
        const auto QueueBefore = F.Queue();
        if (!TestEqual(TEXT("Sale commits before pollution transition"), IShopService::Execute_RequestSell(F.Run.Get(), Secret), EShopActionResult::Sold)) return false;
        const auto Sold = F.Snapshot();
        TestEqual(TEXT("Sale adds exactly ten pollution"), Sold.Pollution, InitialPollution + 10);
        TestEqual(TEXT("Sale grants the price exactly once"), Sold.Money, Before.Money + 60);
        TestEqual(TEXT("Daily income records one price"), Sold.TodayIncome, Before.TodayIncome + 60);
        TestEqual(TEXT("Only one sale is counted"), Sold.TotalSold, Before.TotalSold + 1);
        TestEqual(TEXT("Exactly one owned secret copy remains"), F.Book().Stock, 1);
        TestEqual(TEXT("Listed sold copy is removed"), F.Book().ListedCopies, 0);
        TestEqual(TEXT("Stored secret copy remains untouched"), F.Book().StoredCopies, 1);
        TestTrue(TEXT("Sale resolves the visitor"), F.Queue()[Index].bServed);
        TestEqual(TEXT("Visitor records Sold resolution"), F.Queue()[Index].Resolution, EShopActionResult::Sold);
        TestEqual(TEXT("Active sale is closed before transition"), F.Run->GetActiveCustomerIndex(), INDEX_NONE);
        const auto QueueAfter = F.Queue();
        if (!TestEqual(TEXT("Transition preserves the original queue"), QueueAfter.Num(), QueueBefore.Num())) return false;
        for (int32 Other = 0; Other < QueueBefore.Num(); ++Other)
            if (Other != Index) TestEqual(TEXT("Sale does not resolve another visitor"), QueueAfter[Other].bServed, QueueBefore[Other].bServed);
        if (InitialPollution == 25)
        {
            TestEqual(TEXT("25 to 35 opens Calm after the sale"), Sold.Phase, EGamePhase::Calm);
            TestEqual(TEXT("Light-stage crossing does not end the run"), Sold.Ending, EShopEnding::None);
            if (!TestTrue(TEXT("Skip is sufficient without a decree UI"), IShopService::Execute_RequestSkipDecree(F.Run.Get()).bSucceeded)) return false;
            TestEqual(TEXT("Skip resumes NightShop, not NightSell or Inside"), F.Snapshot().Phase, EGamePhase::NightShop);
            SameResources(*this, Sold, F.Snapshot());
            SameQueue(*this, QueueAfter, F.Queue());
        }
        else
        {
            TestEqual(TEXT("95 to 105 directly ends the run"), Sold.Phase, EGamePhase::End);
            TestEqual(TEXT("Secret sale selects pollution release ending"), Sold.Ending, EShopEnding::PollutionReleased);
            TestFalse(TEXT("Terminal ending cannot be escaped by skipping Calm"), IShopService::Execute_RequestSkipDecree(F.Run.Get()).bSucceeded);
        }
        const auto Resolved = F.Snapshot();
        TestTrue(TEXT("Duplicate secret sale cannot commit after modal or terminal transition"), IShopService::Execute_RequestSell(F.Run.Get(), Secret) != EShopActionResult::Sold);
        SameResources(*this, Resolved, F.Snapshot());
        SameQueue(*this, QueueAfter, F.Queue());
        TestEqual(TEXT("Duplicate call preserves remaining owned secret"), F.Book().Stock, 1);
        TestEqual(TEXT("Duplicate call preserves remaining stored secret"), F.Book().StoredCopies, 1);
        TestEqual(TEXT("Duplicate call preserves the resolved phase"), F.Snapshot().Phase, Resolved.Phase);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
