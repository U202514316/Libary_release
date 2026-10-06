#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShopGameMode.generated.h"

/** Level entry only; changing levels does not create a second business state. */
UCLASS(Blueprintable)
class LIBARY_RELEASE_API AShopGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AShopGameMode();
    virtual void StartPlay() override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore") bool bStartNewRunOnFirstEntry = true;
};
