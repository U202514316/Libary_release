#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ShopEconomy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSecretEnlightenment,"Bookstore.ProgramA.Release.SevenSecretSalesGrantFiveEnlightenment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSecretEnlightenment::RunTest(const FString&)
{
    UDataTable* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/Release/Data/DT_Books.DT_Books"));
    if(!TestNotNull(TEXT("Load saved book rows"),Table))return false;
    int32 Count=0;
    for(FName Id:Table->GetRowNames())
    {
        const FBookData* Book=Table->FindRow<FBookData>(Id,TEXT("SecretEnlightenment"));
        if(!Book||Book->BookType!=EBookType::Secret)continue;
        ++Count;
        TestEqual(TEXT("Guaranteed secret sale chance"),Book->SaleEnlightenChance,1.f);
        TestEqual(TEXT("Secret sale reward"),Book->SaleEnlightenYield,5);
        FShopCatalog C; C.Books.Add(Id,*Book); FShopRunState S; ShopEconomy::Reset(S,C); S.Random.Initialize(731); S.Phase=EGamePhase::Sell;
        FCustomerRuntime Buyer; Buyer.Kind=ECustomerKind::Secret; Buyer.NeedLayer=EBookLayer::Inside; Buyer.NeedType=EBookType::Secret;
        FText Error; if(!ShopEconomy::SetSecretListing(S,C,Id,true,Error))return false;
        const int32 Seed=S.Random.GetCurrentSeed(), Money=S.Money;
        TestEqual(TEXT("Actual listed-copy sale"),ShopEconomy::Sell(S,C,Id,Buyer,Error),EShopActionResult::Sold);
        TestEqual(TEXT("Each successful sale grants exactly five"),S.Enlighten,5);
        TestEqual(TEXT("Sale pays normal authored price"),S.Money,Money+Book->Price);
        TestEqual(TEXT("Guaranteed reward needs no probability roll"),S.Random.GetCurrentSeed(),Seed);
        TestEqual(TEXT("One owned copy cannot sell twice"),ShopEconomy::Sell(S,C,Id,Buyer,Error),EShopActionResult::OutOfStock);
        TestEqual(TEXT("Failed sale gives no extra enlightenment"),S.Enlighten,5);
        ShopEconomy::Reset(S,C); ShopEconomy::SetSecretListing(S,C,Id,true,Error); S.Enlighten=MAX_int32-4;
        TestEqual(TEXT("Enlightenment overflow rejects complete sale"),ShopEconomy::Sell(S,C,Id,Buyer,Error),EShopActionResult::Rejected);
        TestEqual(TEXT("Overflow keeps inventory"),S.Inventory[Id].Stock,Book->InitialStock);
        TestEqual(TEXT("Overflow keeps funds"),S.Money,C.Rules.StartMoney);
    }
    return TestEqual(TEXT("All seven saved secret books covered"),Count,7);
}
#endif
