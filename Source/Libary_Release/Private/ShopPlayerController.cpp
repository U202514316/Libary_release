#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "ShopView.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "ShopAudioComponent.h"

AShopPlayerController::AShopPlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
    ShopAudio = CreateDefaultSubobject<UShopAudioComponent>(TEXT("ShopAudio"));
}

UShopRunSubsystem* AShopPlayerController::GetShopRun() const
{
    UGameInstance* Instance = GetGameInstance();
    return Instance ? Instance->GetSubsystem<UShopRunSubsystem>() : nullptr;
}

void AShopPlayerController::ShopTestEnding(FString Preset)
{
    UShopRunSubsystem* Run = GetShopRun();
    const int64 Value = StaticEnum<EShopEndingTest>()->GetValueByNameString(Preset);
    if (!Run || Value == INDEX_NONE || !Run->PrepareEndingTest(static_cast<EShopEndingTest>(Value)))
        ClientMessage(TEXT("PIE only. Usage: ShopTestEnding EmptyShelf | Pollution | Closed | TruthChoice | Redeemed"));
    else ClientMessage(TEXT("Created an ending test run. The previous run was replaced; normal saved rules are unchanged."));
}

void AShopPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    ShopAudio->InitializeAudio(AudioPalette);
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    // Presentation is selected by the project's Widget Blueprint, never by a native demo override.
    if (RootWidgetClass) AttachShopView(CreateWidget<UUserWidget>(this, RootWidgetClass));
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
