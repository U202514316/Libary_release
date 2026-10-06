#include "ShopGameMode.h"
#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "Engine/GameInstance.h"

AShopGameMode::AShopGameMode()
{
    PlayerControllerClass = AShopPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}

void AShopGameMode::StartPlay()
{
    Super::StartPlay();
    if (!bStartNewRunOnFirstEntry || !GetGameInstance()) return;
    UShopRunSubsystem* Run = GetGameInstance()->GetSubsystem<UShopRunSubsystem>();
    if (Run && Run->GetSnapshot_Implementation().Phase == EGamePhase::Boot)
    {
        if (!Run->RequestNewRun_Implementation())
            UE_LOG(LogTemp, Error, TEXT("Bookstore startup failed: %s"), *Run->GetLastError().ToString());
    }
}
