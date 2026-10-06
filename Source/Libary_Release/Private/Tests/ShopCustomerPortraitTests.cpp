#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopCustomers.h"
#include "ShopEconomy.h"
#include "ShopValidation.h"

namespace ShopCustomerPortraitTests
{
    FShopCatalog Catalog()
    {
        FShopCatalog C;
        C.Rules.bDaytimeOnlyLoop = true;
        C.Rules.bUniqueDailyCustomerPortraits = true;
        FBookData Book; Book.DisplayName = FText::FromString(TEXT("Test novel"));
        Book.Cost = 2; Book.Price = 10; Book.InitialStock = 9;
        C.Books.Add(TEXT("novel"), Book);
        Book.Layer = EBookLayer::Inside; Book.BookType = EBookType::Secret;
        C.Books.Add(TEXT("secret_book"), Book);
        FCustomerData Customer; Customer.DisplayName = FText::FromString(TEXT("Test visitor"));
        for (const ECustomerKind Kind : {ECustomerKind::Normal, ECustomerKind::Hurry, ECustomerKind::Secret, ECustomerKind::Polluted})
        {
            Customer.Kind = Kind;
            C.Customers.Add(FName(*FString::Printf(TEXT("kind_%d"), static_cast<int32>(Kind))), Customer);
        }
        FDecreeData Decree; Decree.DisplayName = FText::FromString(TEXT("Test decree"));
        C.Decrees.Add(TEXT("decree"), Decree);
        return C;
    }

    bool SlotMatches(const FCustomerRuntime& Customer)
    {
        const int32 S = Customer.PortraitSlot;
        switch (Customer.Kind)
        {
        case ECustomerKind::Normal: return S >= 0 && S <= 3;
        case ECustomerKind::Hurry: return S == 4;
        case ECustomerKind::Secret: return S == 5;
        case ECustomerKind::Polluted: return S >= 6 && S <= 7;
        default: return false;
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopUniqueDailyPortraitsTest, "Bookstore.ProgramA.Portraits.DailyUniqueAndEligibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopUniqueDailyPortraitsTest::RunTest(const FString&)
{
    const FShopCatalog C = ShopCustomerPortraitTests::Catalog();
    TSet<int32> SeenSlots;
    for (int32 Mode = 0; Mode < 4; ++Mode)
        for (int32 Seed = 1; Seed <= 128; ++Seed)
        {
            FShopRunState State; ShopEconomy::Reset(State, C);
            State.Random.Initialize(Seed); State.Day = 1 + Seed % 35;
            State.Pollution = (Mode & 1) ? C.Rules.MediumThreshold : 0;
            FText Error;
            if (Mode & 2)
                for (int32 Copy = 0; Copy < 3; ++Copy)
                    if (!TestTrue(TEXT("List real owned secret copies"), ShopEconomy::SetSecretListing(State, C, TEXT("secret_book"), true, Error))) return false;
            FShopRunState Replay = State;
            if (!TestTrue(TEXT("Generate three visitors with enough distinct portraits"), ShopCustomers::GenerateForTime(State, C, false, Error)) ||
                !TestTrue(TEXT("Replay the same day and seed"), ShopCustomers::GenerateForTime(Replay, C, false, Error))) return false;
            TSet<int32> Today;
            if (!TestEqual(TEXT("Daily quota stays three"), State.Customers.Num(), 3)) return false;
            for (int32 Index = 0; Index < State.Customers.Num(); ++Index)
            {
                const FCustomerRuntime& Customer = State.Customers[Index];
                if (!TestTrue(TEXT("Every face matches its customer type"), ShopCustomerPortraitTests::SlotMatches(Customer)) ||
                    !TestFalse(TEXT("No portrait appears twice in one day"), Today.Contains(Customer.PortraitSlot)) ||
                    !TestEqual(TEXT("Portrait identity is deterministic"), Customer.PortraitSlot, Replay.Customers[Index].PortraitSlot) ||
                    !TestEqual(TEXT("Gameplay draw remains deterministic"), Customer.TemplateId, Replay.Customers[Index].TemplateId)) return false;
                if (!(Mode & 1) && !TestTrue(TEXT("Polluted visitor still needs medium pollution"), Customer.Kind != ECustomerKind::Polluted)) return false;
                if (!(Mode & 2) && !TestTrue(TEXT("Secret visitor still needs listed stock"), Customer.Kind != ECustomerKind::Secret)) return false;
                Today.Add(Customer.PortraitSlot); SeenSlots.Add(Customer.PortraitSlot);
            }
            const auto Before = State.Customers;
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (!TestTrue(TEXT("Refusal resolves one queued visitor"), ShopCustomers::Complete(State, Index, EShopActionResult::Rejected))) return false;
                for (int32 Other = 0; Other < 3; ++Other)
                    if (!TestEqual(TEXT("Serving/refusing cannot reshuffle daily portraits"), State.Customers[Other].PortraitSlot, Before[Other].PortraitSlot)) return false;
            }
        }
    return TestEqual(TEXT("All eight supplied customer portraits are reachable"), SeenSlots.Num(), 8);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopPortraitKindQuotaTest, "Bookstore.ProgramA.Portraits.SharedKindCapacityAndLegacyOptOut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopPortraitKindQuotaTest::RunTest(const FString&)
{
    for (const ECustomerKind Dominant : {ECustomerKind::Hurry, ECustomerKind::Secret, ECustomerKind::Polluted})
    {
        FShopCatalog C = ShopCustomerPortraitTests::Catalog();
        // Multiple templates of the same kind must share the same artwork quota.
        FCustomerData Extra; Extra.DisplayName = FText::FromString(TEXT("Duplicate kind"));
        Extra.Kind = Dominant; Extra.SpawnWeight = 1.e12f;
        C.Customers.Add(TEXT("dominant_a"), Extra); C.Customers.Add(TEXT("dominant_b"), Extra);
        FShopRunState State; ShopEconomy::Reset(State, C); State.Random.Initialize(731); State.Pollution = 70;
        FText Error;
        for (int32 Copy = 0; Copy < 3; ++Copy)
            if (!TestTrue(TEXT("Prepare stocked secret demand"), ShopEconomy::SetSecretListing(State, C, TEXT("secret_book"), true, Error))) return false;
        if (!TestTrue(TEXT("Draw excludes the exhausted kind, without reducing headcount"), ShopCustomers::GenerateForTime(State, C, false, Error))) return false;
        int32 KindCount = 0;
        for (const auto& Customer : State.Customers) if (Customer.Kind == Dominant) ++KindCount;
        if (!TestEqual(TEXT("Daily type maximum follows distinct available art"), KindCount, ShopCustomers::PortraitCapacity(Dominant)) ||
            !TestEqual(TEXT("Weighted alternatives fill the rest"), State.Customers.Num(), 3)) return false;
    }
    FShopCatalog C = ShopCustomerPortraitTests::Catalog();
    for (auto& Pair : C.Customers) Pair.Value.SpawnWeight = Pair.Value.Kind == ECustomerKind::Hurry ? 1.f : 0.f;
    FText Error;
    TestFalse(TEXT("Validation rejects configurations unable to fill a unique day"), ShopValidation::Validate(C, Error));
    FShopRunState State; ShopEconomy::Reset(State, C); State.Random.Initialize(731);
    FCustomerRuntime Original; Original.TemplateId = TEXT("preserve_me"); Original.PortraitSlot = 2; State.Customers.Add(Original);
    const int32 BeforeSeed = State.Random.GetCurrentSeed();
    TestFalse(TEXT("Direct generation also fails atomically on portrait exhaustion"), ShopCustomers::GenerateForTime(State, C, false, Error));
    TestEqual(TEXT("Failure retains previous queue"), State.Customers[0].TemplateId, Original.TemplateId);
    TestEqual(TEXT("Failure retains gameplay randomness"), State.Random.GetCurrentSeed(), BeforeSeed);
    C.Rules.bUniqueDailyCustomerPortraits = false;
    if (!TestTrue(TEXT("Legacy catalogs retain repeated-kind behavior"), ShopCustomers::GenerateForTime(State, C, false, Error))) return false;
    for (const auto& Customer : State.Customers)
        if (!TestEqual(TEXT("Legacy repeated Hurry is unchanged"), Customer.PortraitSlot, 4)) return false;
    return true;
}

#endif
