#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShopPlayerController.generated.h"

class UUserWidget;
class UShopRunSubsystem;

/** Optional local UI host. The business state remains owned by ShopRunSubsystem. */
UCLASS(Blueprintable)
class LIBARY_RELEASE_API AShopPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AShopPlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Checked: use the native trading UI. Unchecked: use RootWidgetClass below. Existing widget assets are untouched. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore|UI") bool bUseSecretTradeDemoUI = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore|UI", meta=(EditCondition="!bUseSecretTradeDemoUI")) TSubclassOf<UUserWidget> RootWidgetClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Bookstore|UI") TObjectPtr<UUserWidget> RootWidget;
    UFUNCTION(BlueprintPure, Category="Bookstore|Service") UShopRunSubsystem* GetShopRun() const;
    UFUNCTION(BlueprintCallable, Category="Bookstore|UI") bool AttachShopView(UUserWidget* View);
};
