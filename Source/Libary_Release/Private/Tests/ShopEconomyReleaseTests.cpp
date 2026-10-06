#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopEconomy.h"
#include "ShopCustomers.h"

namespace ShopEconomyReleaseTests
{
    const FName Surface(TEXT("TEST_release_surface"));
    const FName Secret(TEXT("TEST_release_secret"));

    FShopCatalog Catalog()
    {
        FShopCatalog C;
        C.Rules.StartPsychic = 0;
        C.Rules.PsychicMax = 100;
        C.Rules.CollectPsychicCost = 12;
        C.Rules.PollutionOnRead = 5;
        C.Rules.PollutionOnCollect = 5;
        C.Rules.PollutionOnSell = 10;
        C.Rules.PollutionOnPollutedCustomer = 0;
        C.Rules.ClueDropChance = 0.f;
        FBookData Book;
        Book.InitialStock = 64;
        Book.Price = 1;
        Book.SaleEnlightenChance = 0.5f;
        Book.SaleEnlightenYield = 2;
        C.Books.Add(Surface, Book);
        Book = FBookData();
        Book.Layer = EBookLayer::Inside;
        Book.BookType = EBookType::Secret;
        Book.InitialStock = 3;
        Book.InitialOwnedStock = 99; // Legacy data must not be added a second time.
        Book.CollectOfferPerNight = 1;
        Book.Price = 60;
        C.Books.Add(Secret, Book);
        return C;
    }

    FCustomerRuntime Buyer(EBookLayer Layer, ECustomerKind Kind = ECustomerKind::Normal)
    {
        FCustomerRuntime Customer;
        Customer.Kind = Kind;
        Customer.NeedLayer = Layer;
        Customer.NeedType = Layer == EBookLayer::Inside ? EBookType::Secret : EBookType::Novel;
        Customer.bPolluted = Kind == ECustomerKind::Polluted;
        return Customer;
    }

    void AddCustomer(FShopCatalog& C, const TCHAR* Id, ECustomerKind Kind, float Weight)
    {
        FCustomerData Customer;
        Customer.Kind = Kind;
        Customer.SpawnWeight = Weight;
        Customer.PatienceSeconds = 20.f;
        C.Customers.Add(FName(Id), Customer);
    }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopOwnedSecretCopiesTest, "Bookstore.ProgramA.Release.InitialOwnedCopiesAndPsychicCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopOwnedSecretCopiesTest::RunTest(const FString& Parameters)
{
    FShopCatalog C = Catalog();
    FShopRunState State;
    ShopEconomy::Reset(State, C);
    FText Error;
    const FBookRuntime& Initial = State.Inventory.FindChecked(Secret);
    TestEqual(TEXT("InitialStock is owned, with no addition from legacy InitialOwnedStock"), Initial.Stock, 3);
    TestEqual(TEXT("Collection offers are independent from owned stock"), Initial.AvailableToCollect, 1);
    TestEqual(TEXT("Every initial secret copy has a record"), Initial.SecretCopies.Num(), 3);
    TestTrue(TEXT("Initial secret copies are sealed"), Initial.SecretCopies[0].bSealed);
    TestEqual(TEXT("Run starts at zero psychic"), State.Psychic, 0);
    TestTrue(TEXT("A sealed owned book can be read without paying to collect it first"), ShopEconomy::Read(State, C, Secret, Error));
    TestEqual(TEXT("First reading earns psychic"), State.Psychic, C.Rules.ReadPsychicGain);
    TestEqual(TEXT("Reading records exactly one copy"), State.Inventory.FindChecked(Secret).ReadCopies, 1);
    State.Psychic = 99;
    TestTrue(TEXT("Second copy remains readable"), ShopEconomy::Read(State, C, Secret, Error));
    TestEqual(TEXT("Psychic gain is capped at configured maximum"), State.Psychic, 100);
    TestTrue(TEXT("A new copy can be collected"), ShopEconomy::Collect(State, C, Secret, Error));
    const FBookRuntime& Collected = State.Inventory.FindChecked(Secret);
    TestEqual(TEXT("Collect increases owned copies"), Collected.Stock, 4);
    TestEqual(TEXT("Collect consumes the finite nightly offer"), Collected.AvailableToCollect, 0);
    TestTrue(TEXT("Collected copy starts sealed"), Collected.SecretCopies.Last().bSealed);
    TestFalse(TEXT("Collected copy starts unread"), Collected.SecretCopies.Last().bRead);
    const int32 PsychicBefore = State.Psychic;
    const int32 PollutionBefore = State.Pollution;
    TestFalse(TEXT("An exhausted offer cannot be collected again"), ShopEconomy::Collect(State, C, Secret, Error));
    TestEqual(TEXT("Rejected collect preserves psychic"), State.Psychic, PsychicBefore);
    TestEqual(TEXT("Rejected collect preserves pollution"), State.Pollution, PollutionBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPerCopyPollutionTest, "Bookstore.ProgramA.Release.PerCopyReadSaleAndPollutionBonus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPerCopyPollutionTest::RunTest(const FString& Parameters)
{
    FShopCatalog C = Catalog();
    FShopRunState State;
    ShopEconomy::Reset(State, C);
    State.Inventory.FindChecked(Secret).SecretCopies[0].bAltered = true;
    State.Inventory.FindChecked(Secret).SecretCopies[1].bPolluted = true;
    ShopEconomy::RefreshBookCounts(State.Inventory.FindChecked(Secret));
    State.PendingPollutionBonus = 5;
    FText Error;
    TestTrue(TEXT("Read altered copy"), ShopEconomy::Read(State, C, Secret, Error));
    TestEqual(TEXT("One action combines base, alteration and one pending bonus"), State.Pollution, 12);
    TestEqual(TEXT("Pending pollution bonus is consumed once"), State.PendingPollutionBonus, 0);
    const FCustomerRuntime Customer = Buyer(EBookLayer::Inside, ECustomerKind::Normal);
    TestEqual(TEXT("Normal role may buy secret books inside"), static_cast<int32>(ShopEconomy::Sell(State, C, Secret, Customer, Error)), static_cast<int32>(EShopActionResult::Sold));
    TestEqual(TEXT("Already-read copy is sold before unread copies"), State.Inventory.FindChecked(Secret).ReadCopies, 0);
    TestFalse(TEXT("Selling altered copy removes its aggregate alteration flag"), State.Inventory.FindChecked(Secret).bAltered);
    TestTrue(TEXT("Returned copy remains available for reading"), ShopEconomy::Read(State, C, Secret, Error));
    TestEqual(TEXT("Returned copy reading adds its extra pollution"), State.Pollution, 30);
    TestEqual(TEXT("Sell returned read copy"), static_cast<int32>(ShopEconomy::Sell(State, C, Secret, Customer, Error)), static_cast<int32>(EShopActionResult::Sold));
    TestEqual(TEXT("Returned copy sale adds three extra pollution"), State.Pollution, 43);
    TestTrue(TEXT("Final unread copy can be read"), ShopEconomy::Read(State, C, Secret, Error));
    const int32 PollutionBefore = State.Pollution;
    TestFalse(TEXT("A physical copy cannot be read twice by default"), ShopEconomy::Read(State, C, Secret, Error));
    TestEqual(TEXT("Rejected reread does not add pollution"), State.Pollution, PollutionBefore);
    TestEqual(TEXT("Sell final copy"), static_cast<int32>(ShopEconomy::Sell(State, C, Secret, Customer, Error)), static_cast<int32>(EShopActionResult::Sold));
    ShopEconomy::NormalizeSecretCopies(State.Inventory.FindChecked(Secret));
    TestEqual(TEXT("Removing final copy cannot resurrect legacy stock"), State.Inventory.FindChecked(Secret).Stock, 0);
    TestEqual(TEXT("Lost-book pool records every sold physical copy"), State.LostSecretBooks.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopSaleEnlightenmentTest, "Bookstore.ProgramA.Release.SaleEnlightenmentChanceAndRollback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopSaleEnlightenmentTest::RunTest(const FString& Parameters)
{
    FShopCatalog C = Catalog();
    FShopRunState State;
    ShopEconomy::Reset(State, C);
    State.Random.Initialize(731);
    FRandomStream ExpectedRandom(731);
    const FCustomerRuntime Customer = Buyer(EBookLayer::Table);
    int32 ExpectedHits = 0;
    FText Error;
    for (int32 Index = 0; Index < 32; ++Index)
    {
        if (ExpectedRandom.FRand() < 0.5f) ++ExpectedHits;
        if (!TestEqual(TEXT("Successful configured book sale"), static_cast<int32>(ShopEconomy::Sell(State, C, Surface, Customer, Error)), static_cast<int32>(EShopActionResult::Sold))) return false;
    }
    TestTrue(TEXT("Fixed test seed exercises both probability outcomes"), ExpectedHits > 0 && ExpectedHits < 32);
    TestEqual(TEXT("Each 50 percent hit contributes two enlightenment"), State.Enlighten, ExpectedHits * 2);
    TestEqual(TEXT("Only configured sale rolls consume the gameplay stream"), State.Random.GetCurrentSeed(), ExpectedRandom.GetCurrentSeed());
    State.Money = MAX_int32;
    const int32 SeedBefore = State.Random.GetCurrentSeed();
    const int32 StockBefore = State.Inventory.FindChecked(Surface).Stock;
    const int32 EnlightenBefore = State.Enlighten;
    TestEqual(TEXT("Overflowing sale is rejected"), static_cast<int32>(ShopEconomy::Sell(State, C, Surface, Customer, Error)), static_cast<int32>(EShopActionResult::Rejected));
    TestEqual(TEXT("Rejected sale preserves stock"), State.Inventory.FindChecked(Surface).Stock, StockBefore);
    TestEqual(TEXT("Rejected sale preserves enlightenment"), State.Enlighten, EnlightenBefore);
    TestEqual(TEXT("Rejected sale does not consume a chance roll"), State.Random.GetCurrentSeed(), SeedBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopCustomerLayersAndPatienceTest, "Bookstore.ProgramA.Release.CustomerLayersAndLivePatienceRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopCustomerLayersAndPatienceTest::RunTest(const FString& Parameters)
{
    FShopCatalog C = Catalog();
    C.Rules.CustomersMin = C.Rules.CustomersMax = 1000;
    C.Rules.InsideCustomers = 1000;
    C.Rules.WeekTwoCustomerBonus = 0;
    C.Rules.PatienceDropRatePolluted = 0.3f;
    AddCustomer(C, TEXT("TEST_normal"), ECustomerKind::Normal, 5.f);
    AddCustomer(C, TEXT("TEST_hurry"), ECustomerKind::Hurry, 3.f);
    AddCustomer(C, TEXT("TEST_secret"), ECustomerKind::Secret, 2.f);
    AddCustomer(C, TEXT("TEST_polluted"), ECustomerKind::Polluted, 2.f);
    FShopRunState State;
    State.Day = 1;
    State.Pollution = 61;
    State.Random.Initialize(113);
    FText Error;
    TestTrue(TEXT("Generate table customers"), ShopCustomers::Generate(State, C, EBookLayer::Table, Error));
    for (const FCustomerRuntime& Customer : State.Customers)
        if (!TestTrue(TEXT("Only Normal and Hurry can visit table shop"), Customer.Kind == ECustomerKind::Normal || Customer.Kind == ECustomerKind::Hurry)) return false;
    TestTrue(TEXT("Generate inside customers"), ShopCustomers::Generate(State, C, EBookLayer::Inside, Error));
    TSet<ECustomerKind> Kinds;
    for (const FCustomerRuntime& Customer : State.Customers)
    {
        Kinds.Add(Customer.Kind);
        if (!TestTrue(TEXT("All four inside roles request secret books"), Customer.NeedLayer == EBookLayer::Inside && Customer.NeedType == EBookType::Secret)) return false;
    }
    TestEqual(TEXT("Fixed weighted draw includes all four allowed roles"), Kinds.Num(), 4);
    TestEqual(TEXT("Patience remains its full configured duration at spawn"), State.Customers[0].MaxPatience, 20.f);
    const float WaitingPatience = State.Customers[1].Patience;
    ShopCustomers::AdvancePatience(State, C.Rules, 2.f);
    TestTrue(TEXT("Medium pollution drains 2 times 1.3 seconds"), FMath::IsNearlyEqual(State.Customers[0].Patience, 17.4f));
    TestEqual(TEXT("Waiting customers do not lose patience"), State.Customers[1].Patience, WaitingPatience);
    State.Pollution = 60;
    ShopCustomers::AdvancePatience(State, C.Rules, 2.f);
    TestTrue(TEXT("Purifying immediately restores one-times patience rate"), FMath::IsNearlyEqual(State.Customers[0].Patience, 15.4f));
    TestTrue(TEXT("Generate inside customers below medium"), ShopCustomers::Generate(State, C, EBookLayer::Inside, Error));
    for (const FCustomerRuntime& Customer : State.Customers)
        if (!TestTrue(TEXT("Polluted role remains ineligible below threshold"), Customer.Kind != ECustomerKind::Polluted)) return false;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNightIncomeAndPenaltyTest, "Bookstore.ProgramA.Release.NightIncomeAndPermanentBusinessPenalty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopNightIncomeAndPenaltyTest::RunTest(const FString& Parameters)
{
    FShopCatalog C = Catalog();
    C.Books.FindChecked(Secret).Price = 9;
    FShopRunState State;
    ShopEconomy::Reset(State, C);
    State.Day = 1;
    FShopModifier Base;
    Base.Type = EShopEffectType::IncomeMultiplier;
    Base.Multiplier = 0.5f;
    State.Modifiers.Add(Base);
    Base.Type = EShopEffectType::NightIncomeMultiplier;
    Base.Multiplier = 1.5f;
    State.Modifiers.Add(Base);
    const int32 MoneyBefore = State.Money;
    FText Error;
    TestEqual(TEXT("Inside sale succeeds with both multipliers"), static_cast<int32>(ShopEconomy::Sell(State, C, Secret, Buyer(EBookLayer::Inside), Error)), static_cast<int32>(EShopActionResult::Sold));
    TestEqual(TEXT("Combined 9 times .5 times 1.5 is floored once"), State.Money - MoneyBefore, 6);
    const int32 TableMoneyBefore = State.Money;
    TestEqual(TEXT("Surface sale succeeds"), static_cast<int32>(ShopEconomy::Sell(State, C, Surface, Buyer(EBookLayer::Table), Error)), static_cast<int32>(EShopActionResult::Sold));
    TestEqual(TEXT("Night and inside-income multipliers do not affect surface sale"), State.Money - TableMoneyBefore, 1);
    State.RentPenalty = 10;
    const int32 BeforeRent = State.Money;
    TestTrue(TEXT("Rent includes permanent business penalty"), ShopEconomy::PayRent(State, C.Rules, Error));
    TestEqual(TEXT("Charged configured rent plus penalty"), BeforeRent - State.Money, C.Rules.Rent + 10);
    TestFalse(TEXT("Rent remains once per day"), ShopEconomy::PayRent(State, C.Rules, Error));
    C.Rules.CustomersMin = C.Rules.CustomersMax = 5;
    C.Rules.InsideCustomers = 5;
    AddCustomer(C, TEXT("TEST_normal"), ECustomerKind::Normal, 1.f);
    State.CustomerPenalty = 1;
    Base.Type = EShopEffectType::CustomerCountDelta;
    Base.Amount = -1;
    State.Modifiers.Add(Base);
    TestTrue(TEXT("Generate penalized table customers"), ShopCustomers::Generate(State, C, EBookLayer::Table, Error));
    TestEqual(TEXT("Table applies permanent penalty but not night-only delta"), State.Customers.Num(), 4);
    TestTrue(TEXT("Generate penalized inside customers"), ShopCustomers::Generate(State, C, EBookLayer::Inside, Error));
    TestEqual(TEXT("Inside applies permanent penalty and night-only delta"), State.Customers.Num(), 3);
    return true;
}

} // namespace ShopEconomyReleaseTests

#endif
