#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShopPlayerController.generated.h"

class UUserWidget;
class UShopRunSubsystem;
class UShopAudioComponent;
class UShopAudioPalette;
class UShopGameGuideWidget;

/** Optional local UI host. The business state remains owned by ShopRunSubsystem. */
UCLASS(Blueprintable)
class LIBARY_RELEASE_API AShopPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AShopPlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Optional user-authored root widget implementing ShopView. Leave unset to create/register the UI in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore|UI") TSubclassOf<UUserWidget> RootWidgetClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Bookstore|UI") TObjectPtr<UUserWidget> RootWidget;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore|UI") TSubclassOf<UShopGameGuideWidget> GameGuideWidgetClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Bookstore|UI") TObjectPtr<UShopGameGuideWidget> GameGuideWidget;
    UFUNCTION(BlueprintCallable, Category="Bookstore|UI") bool OpenGameGuide();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bookstore|Audio") TObjectPtr<UShopAudioComponent> ShopAudio;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bookstore|Audio") TObjectPtr<UShopAudioPalette> AudioPalette;
    UFUNCTION(BlueprintPure, Category="Bookstore|Service") UShopRunSubsystem* GetShopRun() const;
    /** PIE console: ShopTestEnding EmptyShelf / Pollution / Closed / TruthChoice / Redeemed. */
    UFUNCTION(Exec) void ShopTestEnding(FString Preset);
    UFUNCTION(BlueprintCallable, Category="Bookstore|UI") bool AttachShopView(UUserWidget* View);
};
