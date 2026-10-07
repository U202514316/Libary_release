#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"

namespace FiveEndingTests
{
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Owner{NewObject<UGameInstance>()};
        TStrongObjectPtr<UShopRunSubsystem> Run{NewObject<UShopRunSubsystem>(Owner.Get())};
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        FRunRules Rules;
        FFixture()
        {
            const TCHAR* Names[]={TEXT("DT_Books"),TEXT("DT_Customers"),TEXT("DT_RunRules"),TEXT("DT_Decrees"),TEXT("DT_Endings")};
            for(const TCHAR* Name:Names)
            {
                const FString Path=FString(TEXT("/Game/ProgramA/Release/Data/"))+Name+TEXT(".")+Name;
                UDataTable* Source=LoadObject<UDataTable>(nullptr,*Path);
                Tables.Emplace(Source?DuplicateObject<UDataTable>(Source,Owner.Get()):nullptr);
            }
            if(Tables[2].IsValid())Rules=*Tables[2]->FindRow<FRunRules>(TEXT("Default"),TEXT("EndingTests"));
            Rules.MaxDays=8; Rules.Rent=0; Rules.StartMoney=1500; Rules.StartEnlighten=60; Rules.StartPollution=59;
            Rules.bDaytimeOnlyLoop=true; Rules.bUseCustomerArrivalDelay=false;
            Rules.PollutionDecay=Rules.LightSpreadPerNight=0;
        }
        ~FFixture(){Run->Deinitialize();}
        FRunSnapshot S()const{return Run->GetSnapshot_Implementation();}
        bool ClearCalm(){return S().Phase!=EGamePhase::Calm||Run->RequestSkipDecree_Implementation().bSucceeded;}
        bool Start()
        {
            for(const auto& Table:Tables)if(!Table.IsValid())return false;
            Tables[2]->AddRow(TEXT("Default"),Rules);
            return Run->ConfigureTables(Tables[0].Get(),Tables[1].Get(),Tables[2].Get(),Tables[3].Get(),nullptr,nullptr,nullptr,731,Tables[4].Get())
                &&Run->RequestNewRun_Implementation()&&ClearCalm();
        }
        bool FinishDay()
        {
            while(S().Phase==EGamePhase::Day)
            {
                const auto Queue=Run->GetCustomers_Implementation(); int32 Index=INDEX_NONE;
                for(int32 I=0;I<Queue.Num();++I)if(!Queue[I].bServed){Index=I;break;}
                if(Index==INDEX_NONE||!Run->RequestRejectCustomer_Implementation(Index).bSucceeded||!ClearCalm())return false;
            }
            return S().Phase==EGamePhase::DayEnd&&Run->RequestContinue_Implementation()&&
                Run->RequestOpenInside_Implementation().bSucceeded&&Run->RequestEndNight_Implementation().bSucceeded&&ClearCalm();
        }
        bool Final()
        {
            for(int32 Day=1;Day<=Rules.MaxDays;++Day)
                if(S().Day!=Day||!FinishDay()||!Run->RequestNextDay_Implementation()||!ClearCalm())return false;
            return true;
        }
    };
}
using namespace FiveEndingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveEndingBoundaries,"Bookstore.ProgramA.FiveEndings.FinalBoundariesAndEightDays",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveEndingBoundaries::RunTest(const FString&)
{
    struct FCase{int32 Money,Enlighten,Pollution;EShopEnding Ending;};
    const FCase Cases[]={{1499,60,59,EShopEnding::FailedRedemption},{1500,59,59,EShopEnding::Redeemed},{1500,60,60,EShopEnding::Redeemed}};
    for(const auto& Case:Cases)
    {
        FFixture F; F.Rules.StartMoney=Case.Money;F.Rules.StartEnlighten=Case.Enlighten;F.Rules.StartPollution=Case.Pollution;
        if(!TestTrue(TEXT("Start typed ending boundary fixture"),F.Start()))return false;
        TestEqual(TEXT("Qualifying resources cannot end on day one"),F.S().Ending,EShopEnding::None);
        if(!TestTrue(TEXT("Eight complete nights then final advance"),F.Final()))return false;
        TestEqual(TEXT("Exactly day eight, never day nine"),F.S().Day,8);
        TestEqual(TEXT("Document threshold selects expected ending"),F.S().Ending,Case.Ending);
        const int32 Cost=Case.Ending==EShopEnding::Redeemed?1500:0;
        TestEqual(TEXT("Only successful redemption pays"),F.S().RedemptionPaid,Cost);
        TestEqual(TEXT("Final balance reflects payment once"),F.S().Money,Case.Money-Cost);
        TestFalse(TEXT("Final continue is rejected"),F.Run->RequestNextDay_Implementation());
        if(Case.Ending==EShopEnding::FailedRedemption)
        {
            TestTrue(TEXT("Manuscript substitutes actual balance"),F.S().EndMessage.ToString().Contains(TEXT("1499")));
            TestTrue(TEXT("Manuscript substitutes eight-day test calendar"),F.S().EndMessage.ToString().Contains(TEXT("8天")));
            TestFalse(TEXT("No raw money placeholder"),F.S().EndMessage.ToString().Contains(TEXT("{Money}")));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveEndingChoice,"Bookstore.ProgramA.FiveEndings.ExplicitChoicePaymentPauseAndRestart",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveEndingChoice::RunTest(const FString&)
{
    for(const EShopFinalChoice Choice:{EShopFinalChoice::ReturnTruth,EShopFinalChoice::LeaveCity})
    {
        FFixture F;if(!TestTrue(TEXT("Start qualifying fixture"),F.Start()))return false;
        TestFalse(TEXT("Cannot choose an ending early"),F.Run->RequestChooseEnding_Implementation(Choice).bSucceeded);
        if(!TestTrue(TEXT("Complete all eight days"),F.Final()))return false;
        TestEqual(TEXT("Qualifying ending waits for the player"),F.S().Phase,EGamePhase::EndingChoice);
        TestEqual(TEXT("Truth is not auto-selected"),F.S().Ending,EShopEnding::None);
        TestEqual(TEXT("No payment before consent"),F.S().Money,1500);
        F.Run->Tick(120.f);
        TestEqual(TEXT("Decision time cannot consume resources"),F.S().Money,1500);
        TestFalse(TEXT("Cannot advance past the decision"),F.Run->RequestContinue_Implementation());
        TestFalse(TEXT("Invalid choice cannot resolve or charge"),F.Run->RequestChooseEnding_Implementation(static_cast<EShopFinalChoice>(255)).bSucceeded);
        TestTrue(TEXT("Explicit player choice succeeds"),F.Run->RequestChooseEnding_Implementation(Choice).bSucceeded);
        TestEqual(TEXT("Both branches resolve correctly"),F.S().Ending,Choice==EShopFinalChoice::ReturnTruth?EShopEnding::Returned:EShopEnding::Redeemed);
        TestEqual(TEXT("Redemption paid exactly once"),F.S().Money,0);
        TestFalse(TEXT("Repeated click is rejected"),F.Run->RequestChooseEnding_Implementation(Choice).bSucceeded);
        TestEqual(TEXT("Repeated click cannot charge again"),F.S().Money,0);
        TestTrue(TEXT("New game after either ending"),F.Run->RequestNewRun_Implementation());
        TestEqual(TEXT("Restart clears payment"),F.S().RedemptionPaid,0);
        TestEqual(TEXT("Restart clears ending"),F.S().Ending,EShopEnding::None);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveEndingFailures,"Bookstore.ProgramA.FiveEndings.EarlyFailuresAndEditorPresets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveEndingFailures::RunTest(const FString&)
{
    FFixture Debt;Debt.Rules.StartMoney=1;Debt.Rules.StartPollution=0;Debt.Rules.Rent=2;
    if(!TestTrue(TEXT("Start low-money debt fixture"),Debt.Start()))return false;
    for(int32 Day=1;Day<=3;++Day)
    {
        if(!TestTrue(TEXT("One real negative-balance night"),Debt.FinishDay()))return false;
        if(Day<3){TestEqual(TEXT("Debt grace before third night"),Debt.S().Ending,EShopEnding::None);if(!Debt.Run->RequestNextDay_Implementation())return false;}
    }
    TestEqual(TEXT("Third negative night closes before day eight"),Debt.S().Ending,EShopEnding::Closed);
    TestEqual(TEXT("Bankruptcy does not charge redemption"),Debt.S().RedemptionPaid,0);
    for(const EShopEndingTest Preset:{EShopEndingTest::EmptyShelf,EShopEndingTest::Pollution,EShopEndingTest::Closed,EShopEndingTest::TruthChoice,EShopEndingTest::Redeemed})
    {
        FFixture F;if(!F.Start())return false;
        TestTrue(TEXT("Explicit editor preset creates a transient test run"),F.Run->PrepareEndingTest(Preset));
        TestEqual(TEXT("Editor preset uses current maximum days"),F.S().Day,8);
        const EShopEnding Expected=Preset==EShopEndingTest::EmptyShelf?EShopEnding::FailedRedemption:Preset==EShopEndingTest::Pollution?EShopEnding::PollutionReleased:
            Preset==EShopEndingTest::Closed?EShopEnding::Closed:Preset==EShopEndingTest::Redeemed?EShopEnding::Redeemed:EShopEnding::None;
        TestEqual(TEXT("Preset passes through actual ending resolver"),F.S().Ending,Expected);
        TestEqual(TEXT("Presets do not change configured start money"),F.Run->GetRunRules().StartMoney,1500);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveEndingPollutionPriority,"Bookstore.ProgramA.FiveEndings.PollutionBeforeSimultaneousBankruptcy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveEndingPollutionPriority::RunTest(const FString&)
{
    for(bool LegacyOrder:{false,true})
    {
        FFixture F; F.Rules.StartMoney=0; F.Rules.Rent=1; F.Rules.RentTiming=ERentTiming::NightEnd;
        F.Rules.StartPollution=40; F.Rules.MediumThreshold=97; F.Rules.HeavyThreshold=99; F.Rules.LightSpreadPerNight=20;
        if(LegacyOrder)
        {
            F.Tables[4]->FindRow<FEndingData>(TEXT("Closed"),TEXT("LegacyOrder"))->Priority=1;
            F.Tables[4]->FindRow<FEndingData>(TEXT("PollutionReleased"),TEXT("LegacyOrder"))->Priority=2;
        }
        if(!TestTrue(TEXT("New run accepts current and older saved ending priority orders"),F.Start()))return false;
        for(int32 Day=1;Day<=3;++Day)
        {
            if(!TestTrue(TEXT("Settle a real unpaid night"),F.FinishDay()))return false;
            if(Day<3&&!TestTrue(TEXT("Advance before either failure"),F.Run->RequestNextDay_Implementation()&&F.ClearCalm()))return false;
        }
        TestEqual(TEXT("Both insolvency and pollution conditions reached"),F.S().NegativeDays,3);
        TestEqual(TEXT("Pollution reached one hundred on day three"),F.S().Pollution,100);
        TestEqual(TEXT("Pollution takes precedence over bankruptcy before the final day"),F.S().Ending,EShopEnding::PollutionReleased);
        TestEqual(TEXT("No final-day wait"),F.S().Day,3);
    }
    return true;
}
#endif
