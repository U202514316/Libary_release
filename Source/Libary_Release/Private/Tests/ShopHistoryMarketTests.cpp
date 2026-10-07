#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ShopCustomers.h"
#include "ShopDecrees.h"
#include "ShopEconomy.h"
#include "ShopStory.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace HistoryMarketTests
{
    const FName Secret(TEXT("secret")), Novel(TEXT("novel")), Decree(TEXT("test_decree"));
    FShopCatalog Catalog()
    {
        FShopCatalog C;
        C.Rules.bDaytimeOnlyLoop = true; C.Rules.bApplyDaytimeCustomerPenalty = true;
        C.Rules.bEnableHistory = C.Rules.bUseHistoryFragments = true;
        C.Rules.bEnableMarket = C.Rules.bSecretBookMarket = true;
        C.Rules.bUseCustomerArrivalDelay = true; C.Rules.bAllowEarlyClose = false;
        C.Rules.RandomNeedPool = {EBookType::Novel}; C.Rules.StartMoney = 1000; C.Rules.Rent = 0;
        C.Rules.StartPsychic = 20; C.Rules.ReadPsychicGain = C.Rules.PollutionOnRead = 10;
        C.Rules.PollutionDecay = C.Rules.LightSpreadPerNight = 0; C.Rules.bAdvanceTurnOnStageRise = false;
        FBookData Book; Book.DisplayName = FText::FromString(TEXT("Novel")); Book.InitialStock = 100; Book.Cost = 2; Book.Price = 10;
        C.Books.Add(Novel, Book);
        Book.DisplayName = FText::FromString(TEXT("Secret")); Book.BookType = EBookType::Secret; Book.Layer = EBookLayer::Inside;
        Book.InitialStock = 2; Book.Price = 60; Book.PsychicYield = 8; Book.PollutionYield = 5; C.Books.Add(Secret, Book);
        FCustomerData Customer; Customer.DisplayName = FText::FromString(TEXT("Visitor")); C.Customers.Add(TEXT("normal"), Customer);
        FDecreeData Law; Law.Id = Decree; Law.DisplayName = FText::FromString(TEXT("Test decree")); Law.MinStage = EPollutionStage::Safe;
        Law.PsychicCost = 0; Law.PollutionCut = 0; Law.LoopholeDelay = 2; Law.CooldownTurns = 3;
        Law.LoopholeText = FText::FromString(TEXT("Next pollution +5"));
        FShopEffect Effect; Effect.Type = EShopEffectType::NextPollutionBonus; Effect.Amount = 5; Law.LoopholeEffect.Add(Effect);
        C.Decrees.Add(Decree, Law);
        for(int32 Index=1; Index<=7; ++Index)
        {
            FEventData Page; Page.Id=FName(*FString::Printf(TEXT("history_%02d"),Index));
            Page.Title=FText::FromString(Page.Id.ToString()); Page.Text=FText::FromString(TEXT("完整残页正文\n\n第二段 # 原样保留")); C.Events.Add(Page.Id, Page);
        }
        FMarketItemData Item; Item.Id=Secret; Item.SecretBookId=Secret; Item.DisplayName=Book.DisplayName; Item.Price=30; Item.bOnePerRun=false;
        C.MarketItems.Add(Secret,Item); return C;
    }
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner{NewObject<UGameInstance>()};
        TStrongObjectPtr<UShopRunSubsystem> Run{NewObject<UShopRunSubsystem>(Owner.Get())};
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        template<typename T> UDataTable* Table(const TMap<FName,T>& Rows)
        {
            UDataTable* TBL=NewObject<UDataTable>(Owner.Get()); TBL->RowStruct=T::StaticStruct(); Tables.Emplace(TBL);
            for(const auto& Pair:Rows)TBL->AddRow(Pair.Key,Pair.Value); return TBL;
        }
        bool Start(const FShopCatalog& C, int32 Seed=173)
        {
            TMap<FName,FRunRules> Rules; Rules.Add(TEXT("Default"),C.Rules);
            return Run->ConfigureTables(Table(C.Books),Table(C.Customers),Table(Rules),Table(C.Decrees),Table(C.Events),Table(C.MarketItems),nullptr,Seed)
                && Run->RequestNewRun_Implementation();
        }
        ~FFixture(){ Run->Deinitialize(); }
        FRunSnapshot Snapshot() const {return Run->GetSnapshot_Implementation();}
        bool ToNight()
        {
            for(int32 Guard=0; Guard<20 && Snapshot().Phase==EGamePhase::Day; ++Guard)
            {
                Run->Tick(Snapshot().CustomerArrivalRemaining+.01f);
                const auto Queue=Run->GetCustomers_Implementation(); int32 Index=INDEX_NONE;
                for(int32 I=0; I<Queue.Num(); ++I)if(!Queue[I].bServed){Index=I; break;}
                if(Index==INDEX_NONE || !Run->RequestRejectCustomer_Implementation(Index).bSucceeded)return false;
            }
            return Snapshot().Phase==EGamePhase::DayEnd && Run->RequestContinue_Implementation() && Snapshot().Phase==EGamePhase::DuskChoice;
        }
        bool Inside(){return ToNight() && Run->RequestOpenInside_Implementation().bSucceeded;}
        bool CloseModal()
        {
            if(Snapshot().Phase==EGamePhase::History && !Run->RequestHistoryChoice_Implementation(EHistoryChoice::Witness).bSucceeded)return false;
            if(Snapshot().Phase==EGamePhase::Calm && !Run->RequestSkipDecree_Implementation().bSucceeded)return false;
            return true;
        }
        bool FinishNight()
        {
            return Run->RequestEndNight_Implementation().bSucceeded && CloseModal() && Run->RequestContinue_Implementation();
        }
    };
}
using namespace HistoryMarketTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistoryReadingLimits, "Bookstore.ProgramA.Extensions.ReadingRewardsAndNightLimit", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHistoryReadingLimits::RunTest(const FString&)
{
    for(float Chance:{0.f,1.f})
    {
        FShopCatalog C=Catalog(); C.Rules.HistoryFragmentChance=Chance; FFixture F;
        if(!TestTrue(TEXT("Configure and enter inside"), F.Start(C)&&F.Inside()))return false;
        const auto Before=F.Snapshot();
        if(!TestTrue(TEXT("Read owned copy"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded))return false;
        TestEqual(TEXT("Exactly ten psychic regardless of fragment"),F.Snapshot().Psychic,Before.Psychic+10);
        TestEqual(TEXT("Exactly ten base pollution, overrides old per-book five"),F.Snapshot().Pollution,Before.Pollution+10);
        TestEqual(TEXT("Zero or one page, never one roll per page"),F.Snapshot().CollectedHistoryPages.Num(),Chance==1.f?1:0);
        TestEqual(TEXT("Only a new fragment grants ten enlightenment"),F.Snapshot().Enlighten,Before.Enlighten+(Chance==1.f?10:0));
        if(!TestTrue(TEXT("Close pending page without granting rewards again"),F.CloseModal()))return false;
        TestEqual(TEXT("Closing does not double psychic"),F.Snapshot().Psychic,Before.Psychic+10);
        TestEqual(TEXT("Closing does not double enlightenment"),F.Snapshot().Enlighten,Before.Enlighten+(Chance==1.f?10:0));
        const auto After=F.Snapshot();
        TestFalse(TEXT("Cannot reread another copy of same title tonight"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded);
        TestEqual(TEXT("Failed reread does not add pollution"),F.Snapshot().Pollution,After.Pollution);
        TestEqual(TEXT("Failed reread does not add enlightenment"),F.Snapshot().Enlighten,After.Enlighten);
        if(!TestTrue(TEXT("Next evening"),F.FinishNight()&&F.Inside()))return false;
        TestTrue(TEXT("Same title is readable on a later night"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded);
    }
    FShopCatalog C=Catalog(); FShopRunState State; ShopEconomy::Reset(State,C); State.Day=1; State.Psychic=95; FText Error;
    TestTrue(TEXT("Read across the former psychic cap"),ShopEconomy::Read(State,C,Secret,Error));
    return TestEqual(TEXT("History reading grants all ten psychic above 100"),State.Psychic,105);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistoryUniquenessProbability, "Bookstore.ProgramA.Extensions.SevenUniquePagesAndSingleProbabilityRoll", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHistoryUniquenessProbability::RunTest(const FString&)
{
    FShopCatalog C=Catalog(); FShopRunState State; State.Random.Initialize(831); C.Rules.HistoryFragmentChance=1.f;
    for(int32 Index=0;Index<12;++Index)
    {
        ShopStory::QueueEvents(State,C,EShopEventTrigger::OnRead,Secret);
        TestEqual(TEXT("Acquisition set never exceeds seven"),State.CollectedHistoryPages.Num(),FMath::Min(Index+1,7));
        if(!State.PendingEvents.IsEmpty())
        {
            State.PendingEventId=State.PendingEvents[0]; State.PendingEvents.Reset(); FText Error;
            if(!TestTrue(TEXT("Read and close unique page"),ShopStory::Witness(State,C,EHistoryChoice::Witness,Error)))return false;
        }
    }
    TSet<FName> Unique; for(FName Id:State.CollectedHistoryPages)Unique.Add(Id);
    TestEqual(TEXT("All seven unique originals collected"),Unique.Num(),7);
    C.Rules.HistoryFragmentChance=.3f; int32 Drops=0;
    for(int32 Seed=1;Seed<=10000;++Seed)
    {
        FShopRunState Trial; Trial.Random.Initialize(Seed); ShopStory::QueueEvents(Trial,C,EShopEventTrigger::OnRead,Secret);
        Drops+=Trial.CollectedHistoryPages.Num();
        if(!TestTrue(TEXT("At most one page per reading"),Trial.CollectedHistoryPages.Num()<=1))return false;
    }
    TestTrue(TEXT("Single 30% roll over 10000 seeds, not seven independent rolls"),Drops>2800&&Drops<3200);
    ShopEconomy::Reset(State,C);
    return TestEqual(TEXT("New run resets collected pages"),State.CollectedHistoryPages.Num(),0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistoryModalOrdering, "Bookstore.ProgramA.Extensions.FragmentBeforeThresholdModal", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHistoryModalOrdering::RunTest(const FString&)
{
    FShopCatalog C=Catalog(); C.Rules.StartPollution=25; C.Rules.HistoryFragmentChance=1.f; FFixture F;
    if(!TestTrue(TEXT("Start below light threshold"),F.Start(C)&&F.Inside()))return false;
    if(!TestTrue(TEXT("Read crossing threshold"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded))return false;
    TestEqual(TEXT("New fragment is displayed first"),F.Snapshot().Phase,EGamePhase::History);
    TestTrue(TEXT("Close fragment"),F.Run->RequestHistoryChoice_Implementation(EHistoryChoice::Witness).bSucceeded);
    TestEqual(TEXT("Queued decree modal is not lost"),F.Snapshot().Phase,EGamePhase::Calm);
    TestTrue(TEXT("Close decree modal"),F.Run->RequestSkipDecree_Implementation().bSucceeded);
    TestEqual(TEXT("Return to inside, no customer generation"),F.Snapshot().Phase,EGamePhase::Inside);
    TestEqual(TEXT("Only one reading charge across both modals"),F.Snapshot().Pollution,35);
    return TestFalse(TEXT("Repeated history close is rejected"),F.Run->RequestHistoryChoice_Implementation(EHistoryChoice::Witness).bSucceeded);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarketScheduleAndAtomicity, "Bookstore.ProgramA.Extensions.MarketWeeklyScheduleAndAtomicPurchase", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarketScheduleAndAtomicity::RunTest(const FString&)
{
    FShopCatalog C=Catalog(); FText Error;
    for(int32 Day=1;Day<=35;++Day)
    {
        FShopRunState State; ShopEconomy::Reset(State,C); State.Day=Day; State.Phase=EGamePhase::DuskChoice;
        TestEqual(TEXT("Market available only on seventh nights"),ShopStory::OpenMarket(State,C,Error),Day%7==0);
    }
    FShopRunState State; ShopEconomy::Reset(State,C); State.Day=7; State.Phase=EGamePhase::DuskChoice;
    if(!TestTrue(TEXT("Open first weekly market"),ShopStory::OpenMarket(State,C,Error)))return false;
    const int32 Stock=State.Inventory[Secret].Stock;
    TestTrue(TEXT("Buy secret book"),ShopStory::BuyMarketItem(State,C,Secret,Error));
    TestEqual(TEXT("Price charged"),State.Money,970); TestEqual(TEXT("Purchase pollution five"),State.Pollution,5);
    TestEqual(TEXT("Exactly one book added"),State.Inventory[Secret].Stock,Stock+1);
    TestEqual(TEXT("Bought copy is not auto-listed"),State.Inventory[Secret].ListedCopies,0);
    TestTrue(TEXT("Purchased copy initially sealed and unread"),State.Inventory[Secret].SecretCopies.Last().bSealed&&!State.Inventory[Secret].SecretCopies.Last().bRead);
    TestFalse(TEXT("Offer cannot be double clicked"),ShopStory::BuyMarketItem(State,C,Secret,Error));
    TestEqual(TEXT("No duplicate debit"),State.Money,970);
    State.Day=14; State.Phase=EGamePhase::DuskChoice; State.NightChoice=ENightChoice::None;
    TestTrue(TEXT("Same offer is restocked next week"),ShopStory::OpenMarket(State,C,Error));
    State.Money=29; const auto Before=State;
    TestFalse(TEXT("Unaffordable purchase is atomic"),ShopStory::BuyMarketItem(State,C,Secret,Error));
    TestEqual(TEXT("No pollution charged on rejection"),State.Pollution,Before.Pollution);
    TestEqual(TEXT("No stock granted on rejection"),State.Inventory[Secret].Stock,Before.Inventory[Secret].Stock);
    TestFalse(TEXT("Rejected item remains available"),State.MarketSold.Contains(Secret));
    State.Money=100; TestTrue(TEXT("Retry with sufficient funds"),ShopStory::BuyMarketItem(State,C,Secret,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarketFinalNight, "Bookstore.ProgramA.Extensions.MarketChoiceAndFinalDay35", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMarketFinalNight::RunTest(const FString&)
{
    FFixture F; if(!TestTrue(TEXT("Start 35-day run"),F.Start(Catalog())))return false;
    for(int32 Day=1;Day<=35;++Day)
    {
        if(!TestTrue(TEXT("Complete daytime queue"),F.ToNight()))return false;
        TestEqual(TEXT("Public availability matches day"),F.Run->CanOpenMarket(),Day%7==0);
        if(Day%7==0)
        {
            if(!TestTrue(TEXT("Choose black market"),F.Run->RequestOpenMarket_Implementation().bSucceeded))return false;
            TestFalse(TEXT("Night choice cannot be changed to inside"),F.Run->RequestOpenInside_Implementation().bSucceeded);
            TestFalse(TEXT("Night choice cannot be changed to regular merchant"),F.Run->RequestOpenRestock_Implementation().bSucceeded);
            if(!TestTrue(TEXT("Close market settles night once"),F.Run->RequestCloseMarket_Implementation().bSucceeded))return false;
            TestEqual(TEXT("Market reaches regular night settlement"),F.Snapshot().Phase,EGamePhase::NightEnd);
        }
        else if(!TestTrue(TEXT("Regular activity remains available"),F.Run->RequestOpenInside_Implementation().bSucceeded&&F.Run->RequestEndNight_Implementation().bSucceeded))return false;
        if(!TestTrue(TEXT("Continue night settlement"),F.Run->RequestContinue_Implementation()))return false;
        if(Day<35 && !TestEqual(TEXT("Advance exactly one day"),F.Snapshot().Day,Day+1))return false;
    }
    TestEqual(TEXT("Ends after final market without day36"),F.Snapshot().Day,35);
    return TestEqual(TEXT("End phase reached"),F.Snapshot().Phase,EGamePhase::End);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualDecreePause, "Bookstore.ProgramA.Extensions.ManualDecreeFreezesAndResumesCustomerTimers", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FManualDecreePause::RunTest(const FString&)
{
    FFixture F; if(!TestTrue(TEXT("Start arriving queue"),F.Start(Catalog())))return false;
    const float Remaining=F.Snapshot().CustomerArrivalRemaining;
    TestTrue(TEXT("Open while awaiting first customer"),F.Run->RequestOpenDecrees_Implementation().bSucceeded); F.Run->Tick(120.f);
    TestEqual(TEXT("Arrival countdown frozen"),F.Snapshot().CustomerArrivalRemaining,Remaining);
    TestFalse(TEXT("No visitor appeared in modal"),F.Snapshot().bCustomerPresent);
    TestTrue(TEXT("Close to same waiting state"),F.Run->RequestSkipDecree_Implementation().bSucceeded);
    F.Run->Tick(Remaining+.01f); TestTrue(TEXT("Arrival resumes"),F.Snapshot().bCustomerPresent);
    TestEqual(TEXT("Select current customer"),F.Run->RequestBeginSell_Implementation(0),EShopActionResult::Opened);
    const auto Customer=F.Run->GetCustomers_Implementation()[0];
    TestTrue(TEXT("Open from shelf/sell state"),F.Run->RequestOpenDecrees_Implementation().bSucceeded); F.Run->Tick(120.f);
    TestEqual(TEXT("Patience frozen"),F.Run->GetCustomers_Implementation()[0].Patience,Customer.Patience);
    TestTrue(TEXT("Close decree"),F.Run->RequestSkipDecree_Implementation().bSucceeded);
    TestEqual(TEXT("Returns to selected shelf"),F.Snapshot().Phase,EGamePhase::Sell);
    TestEqual(TEXT("Selected customer retained"),F.Run->GetActiveCustomerIndex(),0);
    F.Run->Tick(.5f); TestTrue(TEXT("Patience resumes"),F.Run->GetCustomers_Implementation()[0].Patience<Customer.Patience);
    F.Run->RequestCancelSell_Implementation();
    if(!TestTrue(TEXT("Enter inside at night"),F.Inside()))return false;
    TestTrue(TEXT("Open inside"),F.Run->RequestOpenDecrees_Implementation().bSucceeded);
    TestTrue(TEXT("Close inside"),F.Run->RequestSkipDecree_Implementation().bSucceeded);
    return TestEqual(TEXT("Returns to inside"),F.Snapshot().Phase,EGamePhase::Inside);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDecreePenaltyAndFeedback, "Bookstore.ProgramA.Extensions.DecreeBacklashCooldownAndPermanentCustomerPenalty", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDecreePenaltyAndFeedback::RunTest(const FString&)
{
    FShopCatalog C=Catalog(); FShopRunState State; ShopEconomy::Reset(State,C); State.Day=1; FText Error;
    for(int32 Penalty=0;Penalty<=5;++Penalty)
    {
        State.CustomerPenalty=Penalty;
        if(!TestTrue(TEXT("Generate under permanent penalty"),ShopCustomers::GenerateForTime(State,C,false,Error)))return false;
        TestEqual(TEXT("Three base visitors reduced but at least one"),State.Customers.Num(),FMath::Max(1,3-Penalty));
    }
    FFixture F; if(!TestTrue(TEXT("Start decree fixture"),F.Start(C)))return false;
    TestTrue(TEXT("Open manual law"),F.Run->RequestOpenDecrees_Implementation().bSucceeded);
    TestTrue(TEXT("Enact actual candidate"),F.Run->RequestEnactDecree_Implementation(Decree).bSucceeded);
    for(int32 Night=1;Night<=2;++Night)
        if(!TestTrue(TEXT("Age law through night settlement"),F.Inside()&&F.FinishNight()))return false;
    const auto Snapshot=F.Snapshot();
    TestEqual(TEXT("One committed backlash feedback"),Snapshot.DecreeBacklashLog.Num(),1);
    TestTrue(TEXT("Law expired and backlash triggered"),!Snapshot.ActiveDecrees[0].bActive&&Snapshot.ActiveDecrees[0].bLoopholeTriggered);
    TestEqual(TEXT("Cooldown begins after backlash"),Snapshot.ActiveDecrees[0].CooldownUntilTurn-Snapshot.Turn,3);
    TestTrue(TEXT("Open during cooldown"),F.Run->RequestOpenDecrees_Implementation().bSucceeded);
    TestFalse(TEXT("Cooldown rejects repeated enact"),F.Run->RequestEnactDecree_Implementation(Decree).bSucceeded);
    TestEqual(TEXT("Rejected enact cannot duplicate feedback"),F.Snapshot().DecreeBacklashLog.Num(),1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistoryEnlightenmentAtomicity, "Bookstore.ProgramA.Extensions.FragmentEnlightenmentOnceAndOverflow", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHistoryEnlightenmentAtomicity::RunTest(const FString&)
{
    FShopCatalog C=Catalog(); C.Rules.HistoryFragmentChance=1.f; C.Rules.PollutionOnRead=0;
    FFixture F;if(!TestTrue(TEXT("Start eight-night collection fixture"),F.Start(C)&&F.Inside()))return false;
    for(int32 Read=1;Read<=8;++Read)
    {
        if(!TestTrue(TEXT("Read and close through actual service"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded&&F.CloseModal()))return false;
        TestEqual(TEXT("Seven unique fragments grant seventy, exhausted pool grants nothing extra"),F.Snapshot().Enlighten,FMath::Min(Read,7)*10);
        if(Read<8&&!TestTrue(TEXT("Advance to another reading night"),F.FinishNight()&&F.Inside()))return false;
    }
    C.Rules.StartEnlighten=MAX_int32-5;
    FFixture Overflow;if(!Overflow.Start(C)||!Overflow.Inside())return false;
    const FRunSnapshot Before=Overflow.Snapshot();
    TestFalse(TEXT("Overflow rejects the entire reading transaction"),Overflow.Run->RequestReadSecret_Implementation(Secret).bSucceeded);
    TestEqual(TEXT("No partial enlightenment on overflow"),Overflow.Snapshot().Enlighten,Before.Enlighten);
    TestEqual(TEXT("No partial psychic on overflow"),Overflow.Snapshot().Psychic,Before.Psychic);
    TestEqual(TEXT("No page consumed on overflow"),Overflow.Snapshot().CollectedHistoryPages.Num(),0);
    FBookRuntime Book; Overflow.Run->GetBookRuntime(Secret,Book);
    TestTrue(TEXT("Failed transaction does not use tonight's reading"),Book.LastReadDay!=Before.Day);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHistoryPollutionTerminal, "Bookstore.ProgramA.Extensions.DayOnePollutionPreemptsFragment", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHistoryPollutionTerminal::RunTest(const FString&)
{
    for(int32 Pollution:{89,90})
    {
        FShopCatalog C=Catalog(); C.Rules.StartPollution=Pollution; C.Rules.HeavyGraceTurns=100; C.Rules.HistoryFragmentChance=1.f;
        FFixture F;if(!TestTrue(TEXT("Start and reach first inside night"),F.Start(C)&&F.CloseModal()&&F.Inside()))return false;
        if(!TestTrue(TEXT("Actual reading adds ten pollution"),F.Run->RequestReadSecret_Implementation(Secret).bSucceeded))return false;
        TestEqual(TEXT("This is day one, not final day"),F.Snapshot().Day,1);
        TestEqual(TEXT("Reward commits once even in the terminal transaction"),F.Snapshot().Enlighten,10);
        if(Pollution==90)
        {
            TestEqual(TEXT("100 immediately opens ending, bypassing page and decree UI"),F.Snapshot().Phase,EGamePhase::End);
            TestEqual(TEXT("100 means pollution ending"),F.Snapshot().Ending,EShopEnding::PollutionReleased);
            TestTrue(TEXT("Terminal state clears pending fragment panel"),F.Snapshot().PendingEventId.IsNone());
            TestFalse(TEXT("Cannot escape terminal ending by opening decrees"),F.Run->RequestOpenDecrees_Implementation().bSucceeded);
        }
        else TestEqual(TEXT("99 allows fragment modal without ending"),F.Snapshot().Ending,EShopEnding::None);
    }
    return true;
}
#endif
