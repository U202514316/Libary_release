#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShopView.h"
#include "ShopSecretTradeWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UWrapBox;
class UShopRunSubsystem;
class UShopSecretTradeWidget;

enum class EShopSecretTradeAction : uint8
{
    NewRun, Continue, EndDay, OpenInside, OpenRestock, OpenTableShop,
    EndNight, BeginSell, CancelSell, Observe, Reject, Sell, Restock,
    ListSecret, UnlistSecret, SkipDecree, Quit
};

/** One retained receiver per button; dynamic delegates never capture stale customer indices. */
UCLASS()
class LIBARY_RELEASE_API UShopSecretTradeClickHandler : public UObject
{
    GENERATED_BODY()
public:
    EShopSecretTradeAction Action = EShopSecretTradeAction::NewRun;
    FName BookId;
    UPROPERTY(Transient) TWeakObjectPtr<UShopSecretTradeWidget> Owner;
    UPROPERTY(Transient) TObjectPtr<UButton> Button;
    UFUNCTION() void HandleClicked();
};

USTRUCT()
struct FShopSecretTradeBookRow
{
    GENERATED_BODY()
    UPROPERTY(Transient) FName BookId;
    UPROPERTY(Transient) FBookData Data;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Panel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StockText;
};

/** Small native UI for the surface-shop trading / secret-book shelving flow. */
UCLASS()
class LIBARY_RELEASE_API UShopSecretTradeWidget : public UUserWidget, public IShopView
{
    GENERATED_BODY()
public:
    virtual void RefreshShop_Implementation(const FRunSnapshot& Snapshot) override;
    virtual void ResolveCustomer_Implementation(int32 CustomerIndex, EShopActionResult Result) override;
    virtual void NewDay_Implementation(int32 Day) override;
    virtual void OpenCalmPanel_Implementation(const TArray<FName>& Candidates, const FRunSnapshot& Snapshot) override;
    virtual void ShowDecreeResult_Implementation(FName DecreeId, const FShopCommandResult& Result) override;
    virtual void ShowObserveResult_Implementation(int32 CustomerIndex, const FCustomerRuntime& Customer) override;
    virtual void ShowHistoryPanel_Implementation(FName EventId, const FEventData& Event) override;
    virtual void ShowEnding_Implementation(EShopEnding Ending, const FRunSnapshot& Snapshot) override;
    virtual void ShowOwlTip_Implementation(FName LineId, const FText& Text) override;
    virtual void OpenMarket_Implementation(const TArray<FName>& ItemIds, const FRunSnapshot& Snapshot) override;

    void ExecuteAction(EShopSecretTradeAction Action, FName BookId);

protected:
    virtual void NativeOnInitialized() override;

private:
    UPROPERTY(Transient) TObjectPtr<UShopRunSubsystem> Run;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PhaseText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CustomerText;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> CustomerPanel;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> InventoryPanel;
    UPROPERTY(Transient) TArray<TObjectPtr<UShopSecretTradeClickHandler>> ClickHandlers;
    UPROPERTY(Transient) TArray<FShopSecretTradeBookRow> BookRows;
    bool bHandlingClick = false;
    bool bInventoryBuilt = false;

    void BuildInterface();
    void EnsureBookRows();
    UTextBlock* AddText(UVerticalBox* Parent, const FText& Text, int32 FontSize = 20);
    void AddAction(UWrapBox* Parent, const TCHAR* Label, EShopSecretTradeAction Action, FName BookId = NAME_None);
    int32 FindHead(const TArray<FCustomerRuntime>& Customers) const;
    void ShowMessage(const FText& Text, bool bSuccess = true);
    void ReportResult(const FShopCommandResult& Result, const TCHAR* SuccessText);
};
