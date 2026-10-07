#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShopGameGuideWidget.generated.h"

class UShopRunSubsystem;

/** Lifecycle only. The layout, text bindings and buttons live in WBP_GameGuide. */
UCLASS(Abstract, Blueprintable)
class LIBARY_RELEASE_API UShopGameGuideWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    explicit UShopGameGuideWidget(const FObjectInitializer& ObjectInitializer);
    UFUNCTION(BlueprintCallable, Category="Bookstore|UI") void CloseGuide();
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
private:
    TWeakObjectPtr<UShopRunSubsystem> PausedRun;
    TWeakObjectPtr<UUserWidget> PausedRoot;
    bool bRootWasEnabled = true;
    void ReleaseReadingPause();
};
