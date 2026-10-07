#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "ShopPresentationLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace
{
    class FMenuPIEProbe : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        bool bQuit, bClicked=false;
        double StartTime=0;
    public:
        FMenuPIEProbe(FAutomationTestBase* InTest,bool InQuit):Test(InTest),bQuit(InQuit){}
        virtual bool Update() override
        {
            if (StartTime==0) StartTime=FPlatformTime::Seconds();
            if (bQuit && bClicked && GEditor && !GEditor->PlayWorld)
            {
                Test->AddInfo(TEXT("PIE_QUIT_VERIFIED: the saved BtnQuit handler ended the real Play In Editor session."));
                return true;
            }
            if (FPlatformTime::Seconds()-StartTime>30)
            {
                Test->AddError(bClicked?TEXT("Quit button did not end PIE within 30 seconds."):TEXT("PIE main menu did not become ready within 30 seconds."));
                if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
                return true;
            }
            if (bClicked || !GEditor || !GEditor->PlayWorld) return false;
            AShopPlayerController* PC=Cast<AShopPlayerController>(GEditor->PlayWorld->GetFirstPlayerController());
            UUserWidget* Root=PC?PC->RootWidget.Get():nullptr;
            if (!Root || !Root->WidgetTree || !Root->IsInViewport()) return false;
            UWidgetSwitcher* Pages=Cast<UWidgetSwitcher>(Root->WidgetTree->FindWidget(TEXT("Pages")));
            UUserWidget* Menu=Cast<UUserWidget>(Root->WidgetTree->FindWidget(TEXT("MenuPage")));
            UButton* Button=Menu?Cast<UButton>(Menu->WidgetTree->FindWidget(bQuit?TEXT("BtnQuit"):TEXT("BtnStart"))):nullptr;
            if (!Test->TestTrue(TEXT("Real PIE opens the actual menu with an enabled, bound button"),
                Pages && Pages->GetActiveWidgetIndex()==0 && Button && Button->GetIsEnabled() && Button->OnClicked.IsBound())) return true;
            bClicked=true;
            Button->OnClicked.Broadcast();
            if (bQuit) return false; // Only the saved QuitGame graph is allowed to stop this PIE session.
            UShopRunSubsystem* Run=PC->GetShopRun();
            if (!Test->TestTrue(TEXT("Start button shows tutorial before starting the business"),
                Pages->GetActiveWidgetIndex()==1 && Run && Run->GetSnapshot_Implementation().Phase==EGamePhase::Boot)) return true;
            UUserWidget* Guide=Cast<UUserWidget>(Root->WidgetTree->FindWidget(TEXT("TutorialPage")));
            UButton* Next=Guide?Cast<UButton>(Guide->WidgetTree->FindWidget(TEXT("BtnNext"))):nullptr;
            if (!Test->TestTrue(TEXT("Actual owl dialogue next button is bound"),Next && Next->OnClicked.IsBound())) return true;
            for (int32 I=0;I<UShopPresentationLibrary::GetTutorialCount();++I) Next->OnClicked.Broadcast();
            const FRunSnapshot Snapshot=Run->GetSnapshot_Implementation();
            Test->TestTrue(TEXT("Completing the actual dialogue enters day 1 of the saved 35-day run"),
                Pages->GetActiveWidgetIndex()==2 && Snapshot.Phase==EGamePhase::Day && Snapshot.Day==1 && Snapshot.MaxDays==35);
            Test->AddInfo(TEXT("PIE_START_VERIFIED: saved menu -> owl dialogue -> actual day-one shop with the root added to the viewport."));
            return true;
        }
    };
    class FWaitMenuPIEStopped : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        double StartTime=0;
    public:
        explicit FWaitMenuPIEStopped(FAutomationTestBase* InTest):Test(InTest){}
        virtual bool Update() override
        {
            if (!GEditor || !GEditor->PlayWorld) return true;
            if (StartTime==0) StartTime=FPlatformTime::Seconds();
            if (FPlatformTime::Seconds()-StartTime<30) return false;
            Test->AddError(TEXT("Could not finish the first PIE session before testing Quit."));
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopMainMenuPIE,"Bookstore.UI.MainMenu.StartAndQuitPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FShopMainMenuPIE::RunTest(const FString&)
{
    if (!GEditor || IsRunningCommandlet() || GEditor->PlayWorld)
    { AddError(TEXT("Run this test in a separate editor process with no active PIE session.")); return false; }
    if (!FEditorFileUtils::LoadMap(FPaths::ProjectContentDir()/TEXT("ProgramA/UI/Maps/L_BookstoreUI.umap"),false,false)) return false;
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FMenuPIEProbe(this,false));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMenuPIEStopped(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FMenuPIEProbe(this,true));
    return true;
}
#endif
