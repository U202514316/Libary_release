#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "ShopView.h"
#include "ShopSecretTradeWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"

AShopPlayerController::AShopPlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

UShopRunSubsystem* AShopPlayerController::GetShopRun() const
{
    UGameInstance* Instance = GetGameInstance();
    return Instance ? Instance->GetSubsystem<UShopRunSubsystem>() : nullptr;
}

void AShopPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    TSubclassOf<UUserWidget> ViewClass = RootWidgetClass;
    if (bUseSecretTradeDemoUI) ViewClass = UShopSecretTradeWidget::StaticClass();
    if (ViewClass) AttachShopView(CreateWidget<UUserWidget>(this, ViewClass));
}

bool AShopPlayerController::AttachShopView(UUserWidget* View)
{
    UShopRunSubsystem* Run = GetShopRun();
    if (!IsLocalController() || !IsValid(View) || !Run || !View->GetClass()->ImplementsInterface(UShopView::StaticClass())) return false;
    const bool bWasVisible = View->IsInViewport();
    if (!bWasVisible) View->AddToViewport();
    if (!Run->RegisterView(View))
    {
        if (!bWasVisible) View->RemoveFromParent();
        return false;
    }
    if (RootWidget && RootWidget != View)
    {
        Run->UnregisterView(RootWidget);
        RootWidget->RemoveFromParent();
    }
    RootWidget = View;
    return true;
}

void AShopPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (RootWidget)
    {
        if (UShopRunSubsystem* Run = GetShopRun()) Run->UnregisterView(RootWidget);
        RootWidget->RemoveFromParent();
        RootWidget = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}
