#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "ShopPlayerController.h"
#include "ShopGameGuideWidget.h"
#include "ShopRunSubsystem.h"
#include "ShopPresentationLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace
{
    template<typename T> T* Widget(UUserWidget* Parent,const TCHAR* Name)
    { return Parent && Parent->WidgetTree ? Cast<T>(Parent->WidgetTree->FindWidget(Name)) : nullptr; }
    class FGuidePIEProbe : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        int32 Step=0, Index=0;
        double Started=0, WaitUntil=0;
        float Arrival=0, Patience=0;
        FRunSnapshot Before;
        bool Click(UUserWidget* Owner,const TCHAR* Name)
        {
            UButton* Button=Widget<UButton>(Owner,Name);
            if (!Test->TestTrue(FString(TEXT("Saved live button: "))+Name,Button && Button->GetIsEnabled() && Button->OnClicked.IsBound())) return false;
            Button->OnClicked.Broadcast(); return true;
        }
        bool Open(AShopPlayerController* PC,UShopRunSubsystem* Run)
        {
            auto* Shop=Widget<UUserWidget>(PC->RootWidget,TEXT("ShopPage"));
            if (!Click(Shop,TEXT("BtnOwlGuide"))) return false;
            return Test->TestTrue(TEXT("Mailbox opens the configured guide, blocks underlying UI and pauses realtime"),PC->GameGuideWidget && PC->GameGuideWidget->IsInViewport() && Run->IsRealtimePaused() && !PC->RootWidget->GetIsEnabled());
        }
        void Unchanged(UShopRunSubsystem* Run)
        {
            const FRunSnapshot After=Run->GetSnapshot_Implementation();
            Test->TestTrue(TEXT("Reading preserves day, phase, logical turn, money, psychic, pollution, enlightenment and stock"),
                Before.Day==After.Day && Before.Phase==After.Phase && Before.Turn==After.Turn && Before.Money==After.Money && Before.Psychic==After.Psychic && Before.Pollution==After.Pollution && Before.Enlighten==After.Enlighten && Before.TotalStock==After.TotalStock);
        }
    public:
        explicit FGuidePIEProbe(FAutomationTestBase* InTest):Test(InTest){}
        virtual bool Update() override
        {
            if (Started==0) Started=FPlatformTime::Seconds();
            if (FPlatformTime::Seconds()-Started>35) { Test->AddError(TEXT("Game guide PIE test timed out.")); return true; }
            if (!GEditor || !GEditor->PlayWorld) return false;
            auto* PC=Cast<AShopPlayerController>(GEditor->PlayWorld->GetFirstPlayerController());
            if (!PC || !PC->RootWidget || !PC->RootWidget->IsInViewport()) return false;
            auto* Run=PC->GetShopRun();
            if (Step==0)
            {
                if (!Click(Widget<UUserWidget>(PC->RootWidget,TEXT("MenuPage")),TEXT("BtnStart"))) return true;
                auto* Tutorial=Widget<UUserWidget>(PC->RootWidget,TEXT("TutorialPage"));
                for(int32 I=0;I<UShopPresentationLibrary::GetTutorialCount();++I) if(!Click(Tutorial,TEXT("BtnNext"))) return true;
                Before=Run->GetSnapshot_Implementation(); Arrival=Before.CustomerArrivalRemaining;
                if (!Test->TestTrue(TEXT("Day 1 begins with a pending 2..4 second arrival"),Before.Day==1 && Before.Phase==EGamePhase::Day && Arrival>=2 && Arrival<=4 && !Run->IsCurrentCustomerPresent()) || !Open(PC,Run)) return true;
                UShopGameGuideWidget* Original=PC->GameGuideWidget;
                Test->TestTrue(TEXT("Repeated open does not create duplicate guides or pause owners"),PC->OpenGameGuide() && Original==PC->GameGuideWidget);
                Run->Tick(3600.f);
                Test->TestEqual(TEXT("One-hour simulated tick cannot consume arrival while reading"),Run->GetSnapshot_Implementation().CustomerArrivalRemaining,Arrival);
                Test->TestFalse(TEXT("Stale gameplay commands are blocked behind the guide"),Run->RequestOpenDecrees_Implementation().bSucceeded);
                if (!Click(PC->GameGuideWidget,TEXT("BtnPollution"))) return true;
                Test->TestEqual(TEXT("Law tab remains interactive while the shop is paused"),Widget<UWidgetSwitcher>(PC->GameGuideWidget,TEXT("GuidePages"))->GetActiveWidgetIndex(),1);
                if (!Click(PC->GameGuideWidget,TEXT("BtnEndings"))) return true;
                WaitUntil=FPlatformTime::Seconds()+3; Step=1; return false;
            }
            if (FPlatformTime::Seconds()<WaitUntil) return false;
            if (Step==1)
            {
                Unchanged(Run);
                Test->TestEqual(TEXT("Real PIE frames do not consume arrival during three seconds of reading"),Run->GetSnapshot_Implementation().CustomerArrivalRemaining,Arrival);
                if (!Click(PC->GameGuideWidget,TEXT("BtnClose"))) return true;
                Test->TestTrue(TEXT("Closing restores underlying UI and releases the pause"),!PC->GameGuideWidget->IsInViewport() && !Run->IsRealtimePaused() && PC->RootWidget->GetIsEnabled());
                Run->Tick(Arrival*.5f);
                Test->TestTrue(TEXT("Arrival resumes from its exact remainder, not a fresh random delay"),FMath::IsNearlyEqual(Run->GetSnapshot_Implementation().CustomerArrivalRemaining,Arrival*.5f));
                Run->Tick(Arrival*.5f+.01f);
                if (!Test->TestTrue(TEXT("Original customer arrives after remaining delay"),Run->IsCurrentCustomerPresent())) return true;
                Index=UShopPresentationLibrary::GetCurrentCustomerIndex(Run);
                Run->Tick(.75f);
                Before=Run->GetSnapshot_Implementation(); Patience=Run->GetCustomers_Implementation()[Index].Patience;
                if (!Open(PC,Run)) return true;
                Run->Tick(3600.f);
                Test->TestEqual(TEXT("One-hour simulated tick cannot consume current customer's patience"),Run->GetCustomers_Implementation()[Index].Patience,Patience);
                WaitUntil=FPlatformTime::Seconds()+3; Step=2; return false;
            }
            Unchanged(Run);
            const float Still=Run->GetCustomers_Implementation()[Index].Patience;
            Test->TestEqual(TEXT("Current customer patience is identical after real PIE reading frames"),Still,Patience);
            FSlateApplication::Get().SetKeyboardFocus(PC->GameGuideWidget->TakeWidget());
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
            Test->TestTrue(TEXT("Escape closes the guide without ending PIE or keeping gameplay paused"),GEditor->PlayWorld && !PC->GameGuideWidget->IsInViewport() && !Run->IsRealtimePaused() && PC->RootWidget->GetIsEnabled());
            Run->Tick(.5f);
            const float Resumed=Run->GetCustomers_Implementation()[Index].Patience;
            Test->TestTrue(TEXT("Patience continues from its saved remainder and decreases by 0.5 seconds"),FMath::IsNearlyEqual(Resumed,Patience-.5f));
            Test->AddInfo(FString::Printf(TEXT("GUIDE_PAUSE_VERIFIED arrivalBefore=%.3f arrivalAfterReading=%.3f patienceBefore=%.3f patienceAfterReading=%.3f patienceAfterResume=%.3f realReadingSeconds=6 simulatedPausedSeconds=7200"),Arrival,Arrival,Patience,Still,Resumed));
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopGameGuidePIE,"Bookstore.UI.GameGuide.PauseAndResumePIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FShopGameGuidePIE::RunTest(const FString&)
{
    if (!GEditor || IsRunningCommandlet() || GEditor->PlayWorld) { AddError(TEXT("Run in a separate editor with no active PIE.")); return false; }
    if (!FEditorFileUtils::LoadMap(FPaths::ProjectContentDir()/TEXT("ProgramA/UI/Maps/L_BookstoreUI.umap"),false,false)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FGuidePIEProbe(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
