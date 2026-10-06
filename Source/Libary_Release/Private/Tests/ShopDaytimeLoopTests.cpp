#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopCustomers.h"
#include "ShopEconomy.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace ShopDaytimeLoopTests
{
    const FName Novel(TEXT("TEST_day_novel"));
    const FName Secret(TEXT("TEST_secret_a"));
    const FName OtherSecret(TEXT("TEST_secret_b"));
    const FName Decree(TEXT("TEST_day_calm"));

    FShopCatalog MakeCatalog()
    {
        FShopCatalog C;
        C.Rules.bDaytimeOnlyLoop = true;
        C.Rules.CustomersMin = C.Rules.CustomersMax = 9; // Ignored by the dedicated three-slot loop.
        C.Rules.WeekTwoCustomerBonus = 7;
        C.Rules.RandomNeedPool = { EBookType::Novel };
        C.Rules.StartPsychic = 8;
        C.Rules.NightlyPsychicGain = 8;
        C.Rules.NightlySecretSupply = 1;
        C.Rules.SecretOwnedCap = 3;
        C.Rules.PollutionDecay = 0;
        C.Rules.LightSpreadPerNight = 0;
        C.Rules.bAdvanceTurnOnStageRise = false;
        C.Rules.Rent = 7;
        C.Rules.MaxDays = 2;
        C.Rules.StartPollution = 5;
        C.Rules.bAllowEarlyClose = false;
        FBookData Book;
        Book.DisplayName = FText::FromString(TEXT("TEST novel"));
        Book.InitialStock = 10; Book.Cost = 2; Book.Price = 20;
        C.Books.Add(Novel, Book);
        Book.DisplayName = FText::FromString(TEXT("TEST secret"));
        Book.Layer = EBookLayer::Inside; Book.BookType = EBookType::Secret;
        Book.InitialStock = 3; Book.Price = 60; Book.CollectOfferPerNight = 0;
        C.Books.Add(Secret, Book);
        FCustomerData Customer;
        Customer.DisplayName = FText::FromString(TEXT("TEST visitor"));
        Customer.NeedLine = FText::FromString(TEXT("TEST {类型}"));
        C.Customers.Add(TEXT("TEST_normal"), Customer);
        Customer.Kind = ECustomerKind::Secret;
        Customer.SpawnWeight = 1.e12f; // Make the authored-stock path deterministic for this fixture seed.
        C.Customers.Add(TEXT("TEST_secret"), Customer);
        FDecreeData Rule;
        Rule.Id = Decree; Rule.DisplayName = FText::FromString(TEXT("TEST calm"));
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
        FBookRuntime Book() const { FBookRuntime Out; Run->GetBookRuntime(Secret, Out); return Out; }
    };
}

using namespace ShopDaytimeLoopTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDaytimeStockAndSupplyTest, "Bookstore.ProgramA.DaytimeLoop.StockAwareVisitorsAndBoundedSupply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDaytimeStockAndSupplyTest::RunTest(const FString&)
{
    FShopCatalog C = MakeCatalog();
    FShopRunState State; ShopEconomy::Reset(State, C);
    State.Day = 20; State.CustomerPenalty = 2; State.Random.Initialize(731);
    FText Error;
    if (!TestTrue(TEXT("Generate the first unlisted day"), ShopCustomers::GenerateForTime(State, C, false, Error))) return false;
    TestEqual(TEXT("Three slots ignore week bonus and permanent customer reduction"), State.Customers.Num(), 3);
    for (const auto& Customer : State.Customers) TestTrue(TEXT("Unlisted stock cannot attract secret demand"), Customer.Kind != ECustomerKind::Secret);
    if (!TestTrue(TEXT("List one actual copy"), ShopEconomy::SetSecretListing(State, C, Secret, true, Error))) return false;
    if (!TestTrue(TEXT("Generate with one listed copy"), ShopCustomers::GenerateForTime(State, C, false, Error))) return false;
    int32 SecretCount = 0;
    for (const auto& Customer : State.Customers)
        if (Customer.Kind == ECustomerKind::Secret)
        {
            ++SecretCount;
            TestEqual(TEXT("Secret demand retains product origin"), Customer.NeedLayer, EBookLayer::Inside);
            TestEqual(TEXT("Secret demand retains product category"), Customer.NeedType, EBookType::Secret);
        }
    TestEqual(TEXT("Only the listed copy can reserve one secret visitor"), SecretCount, 1);
    const int32 Seed = State.Random.GetCurrentSeed();
    TestFalse(TEXT("No nighttime queue in daytime-only mode"), ShopCustomers::GenerateForTime(State, C, true, Error));
    TestEqual(TEXT("Rejected nighttime generation does not consume randomness"), State.Random.GetCurrentSeed(), Seed);
    TestEqual(TEXT("Rejected generation preserves the existing three slots"), State.Customers.Num(), 3);

    C.Books[Secret].InitialStock = 1;
    C.Books.Add(OtherSecret, C.Books[Secret]); C.Books[OtherSecret].InitialStock = 0;
    C.Rules.NightlySecretSupply = 4;
    ShopEconomy::Reset(State, C); State.Psychic = 98;
    TestTrue(TEXT("Existing copy is listed before supply"), ShopEconomy::SetSecretListing(State, C, Secret, true, Error));
    if (!TestTrue(TEXT("Apply bounded balanced supply"), ShopEconomy::ApplyNightlySupply(State, C, Error))) return false;
    TestEqual(TEXT("Psychic recovery clamps at maximum"), State.Psychic, 100);
    TestEqual(TEXT("Lowest stock then lexical tie-breaking selects A twice total"), State.Inventory[Secret].Stock, 2);
    TestEqual(TEXT("Initially empty B receives first supply"), State.Inventory[OtherSecret].Stock, 1);
    TestEqual(TEXT("Existing listing is retained"), State.Inventory[Secret].ListedCopies, 1);
    TestEqual(TEXT("Supply never auto-lists B"), State.Inventory[OtherSecret].ListedCopies, 0);
    TestTrue(TEXT("New B copy is sealed"), State.Inventory[OtherSecret].SecretCopies[0].bSealed);
    TestFalse(TEXT("New B copy is unread"), State.Inventory[OtherSecret].SecretCopies[0].bRead);
    TestFalse(TEXT("New B copy is not polluted"), State.Inventory[OtherSecret].SecretCopies[0].bPolluted);
    TestTrue(TEXT("At-cap supply remains valid"), ShopEconomy::ApplyNightlySupply(State, C, Error));
    TestEqual(TEXT("At-cap supply cannot exceed total three"), State.Inventory[Secret].Stock + State.Inventory[OtherSecret].Stock, 3);
    State.Psychic = 10; ++State.Inventory[Secret].Stock;
    TestFalse(TEXT("Malformed copies reject the entire supply transaction"), ShopEconomy::ApplyNightlySupply(State, C, Error));
    TestEqual(TEXT("Failed supply also rolls back psychic recovery"), State.Psychic, 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDaytimeLoopServiceTest, "Bookstore.ProgramA.DaytimeLoop.ServiceCycleCalmAndRestart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDaytimeLoopServiceTest::RunTest(const FString&)
{
    {
        FShopCatalog C = MakeCatalog();
        C.Rules.StartPollution = C.Rules.LightThreshold;
        FShopEffect FakeEffect; FakeEffect.Type = EShopEffectType::SpawnFakeCustomer; FakeEffect.Amount = 3;
        C.Decrees[Decree].Effects.Add(FakeEffect);
        FFixture Fakes;
        if (!TestTrue(TEXT("Start fake-slot fixture in Calm"), Fakes.Start(C))) return false;
        if (!TestTrue(TEXT("Enact configured fake-customer effect"), IShopService::Execute_RequestEnactDecree(Fakes.Run.Get(), Decree).bSucceeded)) return false;
        TestEqual(TEXT("Three pending fakes never append a fourth visitor"), Fakes.Queue().Num(), 3);
        TestFalse(TEXT("The current head is never replaced"), Fakes.Queue()[0].bFake);
        TestTrue(TEXT("Waiting ordinary slots can become fake customers"), Fakes.Queue()[1].bFake && Fakes.Queue()[2].bFake);
        for (int32 Index = 0; Index < 3; ++Index)
            if (!TestTrue(TEXT("Resolve each of the original three slots"), IShopService::Execute_RequestRejectCustomer(Fakes.Run.Get(), Index).bSucceeded)) return false;
        TestEqual(TEXT("Pending fake without a free slot cannot stop DayEnd"), Fakes.Snapshot().Phase, EGamePhase::DayEnd);
        if (!TestTrue(TEXT("Continue fake-slot night"), IShopService::Execute_RequestContinue(Fakes.Run.Get())) ||
            !TestTrue(TEXT("Choose merchant for fake-slot night"), IShopService::Execute_RequestOpenRestock(Fakes.Run.Get()).bSucceeded) ||
            !TestTrue(TEXT("Settle fake-slot night"), IShopService::Execute_RequestEndNight(Fakes.Run.Get()).bSucceeded) ||
            !TestTrue(TEXT("Generate next day with pending fake"), IShopService::Execute_RequestContinue(Fakes.Run.Get()))) return false;
        TestEqual(TEXT("Pending fake still preserves next day's three slots"), Fakes.Queue().Num(), 3);
        int32 FakesToday = 0;
        for (const auto& Customer : Fakes.Queue()) if (Customer.bFake) ++FakesToday;
        TestEqual(TEXT("Only the one deferred fake replaces a next-day slot"), FakesToday, 1);
    }
    FFixture F;
    if (!TestTrue(TEXT("Start daytime-only service fixture"), F.Start())) return false;
    TestFalse(TEXT("Cannot skip the three initial visitors"), IShopService::Execute_RequestEndDay(F.Run.Get()));
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!TestEqual(TEXT("Open ordinary daytime sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened)) return false;
        if (!TestEqual(TEXT("Complete ordinary daytime sale"), IShopService::Execute_RequestSell(F.Run.Get(), Novel), EShopActionResult::Sold)) return false;
    }
    TestEqual(TEXT("Third visitor automatically reaches DayEnd"), F.Snapshot().Phase, EGamePhase::DayEnd);
    TestEqual(TEXT("Day rent charged exactly once"), F.Snapshot().Money, 100 + 60 - 7);
    TestEqual(TEXT("Resolved queue remains available for callbacks"), F.Queue().Num(), 3);
    TestTrue(TEXT("Last resolved result remains available"), F.Queue()[2].bServed && F.Queue()[2].Resolution == EShopActionResult::Sold);
    TestFalse(TEXT("Duplicate end-day request cannot charge rent again"), IShopService::Execute_RequestEndDay(F.Run.Get()));
    TestEqual(TEXT("Duplicate close leaves money unchanged"), F.Snapshot().Money, 153);
    if (!TestTrue(TEXT("Open mutually exclusive night choices"), IShopService::Execute_RequestContinue(F.Run.Get()))) return false;
    if (!TestTrue(TEXT("Select inside stock management"), IShopService::Execute_RequestOpenInside(F.Run.Get()).bSucceeded)) return false;
    for (int32 Index = 0; Index < 3; ++Index)
        if (!TestTrue(TEXT("List an owned copy for tomorrow"), IShopService::Execute_RequestListSecretBook(F.Run.Get(), Secret).bSucceeded)) return false;
    TestFalse(TEXT("Night trading entry is blocked"), IShopService::Execute_RequestOpenTableShop(F.Run.Get()).bSucceeded);
    TestFalse(TEXT("Cannot switch to merchant after choosing inside"), IShopService::Execute_RequestOpenRestock(F.Run.Get()).bSucceeded);
    if (!TestTrue(TEXT("Settle first night without trading"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded)) return false;
    TestEqual(TEXT("Night restores eight psychic"), F.Snapshot().Psychic, 16);
    TestEqual(TEXT("Full ownership prevents extra supply"), F.Book().Stock, 3);
    TestFalse(TEXT("Night settlement cannot repeat"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded);
    TestEqual(TEXT("Rejected repeat restores no extra psychic"), F.Snapshot().Psychic, 16);
    if (!TestTrue(TEXT("Begin second daytime queue"), IShopService::Execute_RequestContinue(F.Run.Get()))) return false;
    TestEqual(TEXT("Listings survive the next day"), F.Book().ListedCopies, 3);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!TestEqual(TEXT("Stock-backed fixture visitor is Secret"), F.Queue()[Index].Kind, ECustomerKind::Secret)) return false;
        if (!TestEqual(TEXT("Secret visitor opens daytime sale"), IShopService::Execute_RequestBeginSell(F.Run.Get(), Index), EShopActionResult::Opened)) return false;
        if (!TestEqual(TEXT("Listed secret sells during daytime"), IShopService::Execute_RequestSell(F.Run.Get(), Secret), EShopActionResult::Sold)) return false;
    }
    TestEqual(TEXT("Final visitor crosses pollution threshold into Calm"), F.Snapshot().Phase, EGamePhase::Calm);
    TestEqual(TEXT("Second day's rent is already paid once"), F.Snapshot().Money, 153 + 180 - 7);
    TestEqual(TEXT("Secret copies have actually been sold"), F.Book().Stock, 0);
    TestTrue(TEXT("Final secret result survives Calm"), F.Queue()[2].bServed && F.Queue()[2].Resolution == EShopActionResult::Sold);
    if (!TestTrue(TEXT("Use a payable formal decree"), IShopService::Execute_RequestEnactDecree(F.Run.Get(), Decree).bSucceeded)) return false;
    TestEqual(TEXT("Calm restores DayEnd after the final visitor"), F.Snapshot().Phase, EGamePhase::DayEnd);
    TestEqual(TEXT("Decree spends its authored psychic cost"), F.Snapshot().Psychic, 8);
    if (!TestTrue(TEXT("Continue to the second night"), IShopService::Execute_RequestContinue(F.Run.Get()))) return false;
    if (!TestTrue(TEXT("Choose merchant this night"), IShopService::Execute_RequestOpenRestock(F.Run.Get()).bSucceeded)) return false;
    TestTrue(TEXT("Merchant can sell one ordinary book"), IShopService::Execute_RequestRestock(F.Run.Get(), Novel));
    if (!TestTrue(TEXT("Settle merchant night"), IShopService::Execute_RequestEndNight(F.Run.Get()).bSucceeded)) return false;
    TestEqual(TEXT("Exactly one secret copy replenishes below the cap"), F.Book().Stock, 1);
    TestEqual(TEXT("New supply is stored for a future night choice"), F.Book().ListedCopies, 0);
    if (!TestTrue(TEXT("Final day proceeds to ending"), IShopService::Execute_RequestContinue(F.Run.Get()))) return false;
    TestEqual(TEXT("The bounded run ends"), F.Snapshot().Phase, EGamePhase::End);
    if (!TestTrue(TEXT("Restart from ending"), IShopService::Execute_RequestNewRun(F.Run.Get()))) return false;
    TestEqual(TEXT("Restart returns to Day"), F.Snapshot().Phase, EGamePhase::Day);
    TestEqual(TEXT("Restart restores initial stock"), F.Book().Stock, 3);
    TestEqual(TEXT("Restart clears previous listings"), F.Book().ListedCopies, 0);
    TestEqual(TEXT("Restart restores configured initial psychic"), F.Snapshot().Psychic, 8);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
