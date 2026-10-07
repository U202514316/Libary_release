#include "ShopGameGuideWidget.h"
#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "ShopAudioComponent.h"
#include "InputCoreTypes.h"

UShopGameGuideWidget::UShopGameGuideWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bIsFocusable = true;
}

void UShopGameGuideWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (AShopPlayerController* PC = GetOwningPlayer<AShopPlayerController>())
    {
        if (!PausedRun.IsValid())
        {
            PausedRun = PC->GetShopRun();
            if (PausedRun.IsValid()) PausedRun->PauseRealtimeFor(this);
            PausedRoot = PC->RootWidget;
            if (PausedRoot.IsValid())
            {
                bRootWasEnabled = PausedRoot->GetIsEnabled();
                PausedRoot->SetIsEnabled(false);
            }
        }
        if (!IsRunningCommandlet())
        {
            FInputModeUIOnly Mode;
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            Mode.SetWidgetToFocus(TakeWidget());
            PC->SetInputMode(Mode);
            SetKeyboardFocus();
        }
    }
}

void UShopGameGuideWidget::ReleaseReadingPause()
{
    const bool bOwnedPause = PausedRun.IsValid();
    if (PausedRun.IsValid()) PausedRun->ResumeRealtimeFor(this);
    if (PausedRoot.IsValid()) PausedRoot->SetIsEnabled(bRootWasEnabled);
    PausedRun.Reset(); PausedRoot.Reset();
    if (bOwnedPause && !IsRunningCommandlet())
        if (AShopPlayerController* PC = GetOwningPlayer<AShopPlayerController>())
        {
            FInputModeGameAndUI Mode;
            Mode.SetHideCursorDuringCapture(false);
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
        }
}

void UShopGameGuideWidget::CloseGuide()
{
    ReleaseReadingPause();
    RemoveFromParent();
}

void UShopGameGuideWidget::NativeDestruct()
{
    ReleaseReadingPause();
    Super::NativeDestruct();
}

FReply UShopGameGuideWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    if (KeyEvent.GetKey() == EKeys::Escape) { CloseGuide(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, KeyEvent);
}
