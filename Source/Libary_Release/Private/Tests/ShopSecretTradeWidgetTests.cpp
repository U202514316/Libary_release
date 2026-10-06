#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ShopSecretTradeWidget.h"
#include "ShopRunSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

namespace ShopSecretTradeWidgetTests
{
    const FName Novel(TEXT("UI_TEST_novel"));
    const FName Secret(TEXT("UI_TEST_secret"));

    // No authored level, Blueprint class, viewport, BeginPlay or saved package is used.
    // InitializeStandalone supplies the world/subsystem context required by CreateWidget.
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine, NAME_None, RF_Transient)};
        TStrongObjectPtr<UShopSecretTradeWidget> View;
        TArray<TStrongObjectPtr<UDataTable>> Tables;
        UWorld* World = nullptr;
        UShopRunSubsystem* Run = nullptr;

        FFixture()
        {
            Instance->InitializeStandalone();
            World = Instance->GetWorld();
            Run = Instance->GetSubsystem<UShopRunSubsystem>();
        }

        ~FFixture()
        {
            if (Run && View.IsValid()) Run->UnregisterView(View.Get());
            View.Reset();
            Instance->Shutdown();
            if (World)
            {
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }

        template<typename T> UDataTable* MakeTable()
        {
            UDataTable* Table = NewObject<UDataTable>(Instance.Get(), NAME_None, RF_Transient);
            Table->RowStruct = T::StaticStruct();
            Tables.Emplace(Table);
            return Table;
        }

        bool Configure()
        {
            if (!World || !Run) return false;
            UDataTable* Books = MakeTable<FBookData>();
            UDataTable* Customers = MakeTable<FCustomerData>();
            UDataTable* RulesTable = MakeTable<FRunRules>();
            UDataTable* Decrees = MakeTable<FDecreeData>();
            FBookData Book;
            Book.DisplayName = FText::FromName(Novel);
            Book.InitialStock = 2;
            Books->AddRow(Novel, Book);
            Book.DisplayName = FText::FromName(Secret);
            Book.BookType = EBookType::Secret;
            Book.Layer = EBookLayer::Inside;
            Book.CollectOfferPerNight = 0;
            Books->AddRow(Secret, Book);
            FCustomerData Customer;
            Customer.DisplayName = FText::FromString(TEXT("UI TEST normal"));
            Customer.NeedLine = FText::FromString(TEXT("UI TEST {类型}"));
            Customers->AddRow(TEXT("UI_TEST_normal"), Customer);
            Customer.Kind = ECustomerKind::Secret;
            Customers->AddRow(TEXT("UI_TEST_secret_customer"), Customer);
            FRunRules Rules;
            Rules.CustomersMin = Rules.CustomersMax = 1;
            Rules.WeekTwoCustomerBonus = 0;
            Rules.InsideCustomers = 2;
            Rules.RandomNeedPool = {EBookType::Novel};
            Rules.Rent = 0;
            Rules.LightSpreadPerNight = 0;
            Rules.PollutionDecay = 0;
            Rules.bEnableHistory = false;
            Rules.bEnableMarket = false;
            RulesTable->AddRow(TEXT("Default"), Rules);
            FDecreeData Decree;
            Decree.Id = TEXT("UI_TEST_decree");
            Decree.DisplayName = FText::FromString(TEXT("UI TEST decree"));
            Decrees->AddRow(Decree.Id, Decree);
            return Run->ConfigureTables(Books, Customers, RulesTable, Decrees, nullptr, nullptr, nullptr, 731);
        }

        bool CreateView()
        {
            // Use native classes explicitly; project default controller/player classes are irrelevant.
            ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
            const FPlatformUserId UserId = FGenericPlatformMisc::GetPlatformUserForUserIndex(0);
            if (Instance->AddLocalPlayer(Player, UserId) == INDEX_NONE) return false;
            APlayerController* Controller = World->SpawnActor<APlayerController>();
            if (!Controller) return false;
            Controller->SetPlayer(Player);
            View.Reset(CreateWidget<UShopSecretTradeWidget>(Instance.Get(), UShopSecretTradeWidget::StaticClass()));
            return View.IsValid();
        }

        UShopSecretTradeClickHandler* FindAction(EShopSecretTradeAction Action, FName BookId = NAME_None) const
        {
            TArray<UObject*> Children;
            GetObjectsWithOuter(View.Get(), Children, false);
            for (UObject* Child : Children)
                if (UShopSecretTradeClickHandler* Handler = Cast<UShopSecretTradeClickHandler>(Child))
                    if (Handler->Action == Action && Handler->BookId == BookId) return Handler;
            return nullptr;
        }

        FRunSnapshot Snapshot() const { return IShopService::Execute_GetSnapshot(Run); }
        FBookRuntime SecretBook() const { FBookRuntime Book; Run->GetBookRuntime(Secret, Book); return Book; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopNativeWidgetSmokeTest, "Bookstore.ProgramA.UI.NativeSecretTradeSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShopNativeWidgetSmokeTest::RunTest(const FString& Parameters)
{
    using namespace ShopSecretTradeWidgetTests;
    if (!TestNotNull(TEXT("Engine is available"), GEngine) ||
        !TestTrue(TEXT("Engine Slate application is initialized"), FSlateApplication::IsInitialized())) return false;
    FFixture F;
    if (!TestTrue(TEXT("Configure deterministic in-memory tables"), F.Configure()) ||
        !TestTrue(TEXT("Create native widget with a valid local-player context"), F.CreateView())) return false;
    if (!TestNotNull(TEXT("Native initialization builds a widget tree"), F.View->WidgetTree.Get()) ||
        !TestNotNull(TEXT("Native initialization builds a root"), F.View->GetRootWidget())) return false;

    TArray<UWidget*> OriginalWidgets;
    F.View->WidgetTree->GetAllWidgets(OriginalWidgets);
    int32 ScrollCount = 0;
    int32 BookTitles = 0;
    for (UWidget* Widget : OriginalWidgets)
    {
        if (Widget->IsA<UScrollBox>()) ++ScrollCount;
        if (const UTextBlock* Label = Cast<UTextBlock>(Widget))
        {
            const FString Value = Label->GetText().ToString();
            if (Value.StartsWith(Novel.ToString()) || Value.StartsWith(Secret.ToString())) ++BookTitles;
        }
    }
    TestEqual(TEXT("One scroll container is constructed"), ScrollCount, 1);
    TestEqual(TEXT("Both catalog books have inventory title widgets"), BookTitles, 2);
    UShopSecretTradeClickHandler* NewRun = F.FindAction(EShopSecretTradeAction::NewRun);
    UShopSecretTradeClickHandler* ListBook = F.FindAction(EShopSecretTradeAction::ListSecret, Secret);
    UShopSecretTradeClickHandler* OpenFront = F.FindAction(EShopSecretTradeAction::OpenTableShop);
    if (!TestNotNull(TEXT("New-run button handler exists"), NewRun) ||
        !TestNotNull(TEXT("Secret-listing button handler exists"), ListBook) ||
        !TestNotNull(TEXT("Return-to-front button handler exists"), OpenFront)) return false;
    TestTrue(TEXT("Listing uses a bound dynamic click delegate"), ListBook->Button->OnClicked.IsBound());

    // The widget must not self-register during initialization. The controller owns registration.
    if (!TestTrue(TEXT("Start run through authoritative service"), IShopService::Execute_RequestNewRun(F.Run))) return false;
    TestEqual(TEXT("Unregistered view retains its Boot presentation"), NewRun->Button->GetVisibility(), ESlateVisibility::Visible);
    if (!TestTrue(TEXT("Register the view exactly once"), F.Run->RegisterView(F.View.Get()))) return false;
    TestEqual(TEXT("Registration synchronizes the current phase"), NewRun->Button->GetVisibility(), ESlateVisibility::Collapsed);

    const FRunSnapshot BeforeRefresh = F.Snapshot();
    for (int32 Index = 0; Index < 5; ++Index) IShopView::Execute_RefreshShop(F.View.Get(), F.Snapshot());
    TestEqual(TEXT("Refresh does not advance the phase"), F.Snapshot().Phase, BeforeRefresh.Phase);
    TestEqual(TEXT("Refresh does not change inventory"), F.Snapshot().TotalStock, BeforeRefresh.TotalStock);
    TestEqual(TEXT("Refresh does not change money"), F.Snapshot().Money, BeforeRefresh.Money);

    F.View->ExecuteAction(EShopSecretTradeAction::EndDay, NAME_None);
    TestEqual(TEXT("UI action closes daytime business"), F.Snapshot().Phase, EGamePhase::DayEnd);
    F.View->ExecuteAction(EShopSecretTradeAction::Continue, NAME_None);
    F.View->ExecuteAction(EShopSecretTradeAction::OpenInside, NAME_None);
    if (!TestEqual(TEXT("UI action enters inside management"), F.Snapshot().Phase, EGamePhase::Inside)) return false;
    TestTrue(TEXT("Owned stored book enables listing"), ListBook->Button->GetIsEnabled());
    ListBook->Button->OnClicked.Broadcast();
    TestEqual(TEXT("One actual click lists exactly one copy"), F.SecretBook().ListedCopies, 1);
    TestEqual(TEXT("One actual click leaves one stored copy"), F.SecretBook().StoredCopies, 1);
    TestEqual(TEXT("Listing preserves ownership"), F.SecretBook().Stock, 2);
    ListBook->Button->OnClicked.Broadcast();
    TestEqual(TEXT("Second actual click lists the second copy"), F.SecretBook().ListedCopies, 2);
    TestFalse(TEXT("No stored inventory disables listing"), ListBook->Button->GetIsEnabled());
    F.View->ExecuteAction(EShopSecretTradeAction::UnlistSecret, Secret);
    TestEqual(TEXT("UI action unlists one copy"), F.SecretBook().ListedCopies, 1);
    F.View->ExecuteAction(EShopSecretTradeAction::OpenTableShop, NAME_None);
    TestEqual(TEXT("UI action returns to nighttime front shop"), F.Snapshot().Phase, EGamePhase::NightShop);

    TArray<UWidget*> RefreshedWidgets;
    F.View->WidgetTree->GetAllWidgets(RefreshedWidgets);
    TestTrue(TEXT("Repeated refresh and actions preserve every widget object, including buttons"), OriginalWidgets == RefreshedWidgets);
    TestTrue(TEXT("Listing retains the same click receiver"), F.FindAction(EShopSecretTradeAction::ListSecret, Secret) == ListBook);
    F.Run->UnregisterView(F.View.Get());
    if (!TestTrue(TEXT("Service can return to inside after unregister"), IShopService::Execute_RequestOpenInside(F.Run).bSucceeded)) return false;
    TestEqual(TEXT("Unregistered view no longer receives phase refreshes"), OpenFront->Button->GetVisibility(), ESlateVisibility::Collapsed);
    return true;
}

#endif
