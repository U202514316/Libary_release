#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ShopService.h"
#include "ShopRunSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FShopChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FShopCustomerResolved, int32, CustomerIndex, EShopActionResult, Result);

/** Authoritative single-player run. Mutations copy, validate and commit state before notifications. */
UCLASS(BlueprintType)
class LIBARY_RELEASE_API UShopRunSubsystem : public UGameInstanceSubsystem, public IShopService, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual TStatId GetStatId() const override;

    virtual bool RequestNewRun_Implementation() override;
    virtual FRunSnapshot GetSnapshot_Implementation() const override;
    virtual TArray<FCustomerRuntime> GetCustomers_Implementation() const override;
    virtual bool GetBookInfo_Implementation(FName BookId, FBookData& BookData, int32& Stock) const override;
    virtual EShopActionResult RequestBeginSell_Implementation(int32 CustomerIndex) override;
    virtual EShopActionResult RequestSell_Implementation(FName BookId) override;
    virtual bool RequestCancelSell_Implementation() override;
    virtual bool RequestEndDay_Implementation() override;
    virtual bool RequestContinue_Implementation() override;
    virtual bool RequestRestock_Implementation(FName BookId) override;
    virtual bool RequestNextDay_Implementation() override;
    virtual FShopCommandResult RequestObserveCustomer_Implementation(int32 CustomerIndex) override;
    virtual FShopCommandResult RequestRejectCustomer_Implementation(int32 CustomerIndex) override;
    virtual FShopCommandResult RequestOpenInside_Implementation() override;
    virtual FShopCommandResult RequestOpenTableShop_Implementation() override;
    virtual FShopCommandResult RequestListSecretBook_Implementation(FName BookId) override;
    virtual FShopCommandResult RequestUnlistSecretBook_Implementation(FName BookId) override;
    virtual FShopCommandResult RequestOpenRestock_Implementation() override;
    virtual FShopCommandResult RequestCollectSecret_Implementation(FName BookId) override;
    virtual FShopCommandResult RequestReadSecret_Implementation(FName BookId) override;
    virtual FShopCommandResult RequestEndNight_Implementation() override;
    virtual FShopCommandResult RequestPurify_Implementation(int32 Amount) override;
    virtual FShopCommandResult RequestEnactDecree_Implementation(FName DecreeId) override;
    virtual FShopCommandResult RequestSkipDecree_Implementation() override;
    virtual FShopCommandResult RequestHistoryChoice_Implementation(EHistoryChoice Choice) override;
    virtual FShopCommandResult RequestOwlTalk_Implementation() override;
    virtual FShopCommandResult RequestOpenMarket_Implementation() override;
    virtual FShopCommandResult RequestBuyMarketItem_Implementation(FName Id) override;
    virtual FShopCommandResult RequestCloseMarket_Implementation() override;

    UFUNCTION(BlueprintCallable, Category="Bookstore|Config")
    bool ConfigureTables(UDataTable* Books, UDataTable* Customers, UDataTable* Rules, UDataTable* Decrees, UDataTable* Events = nullptr, UDataTable* Market = nullptr, UDataTable* Owl = nullptr, int32 Seed = -1, UDataTable* Endings = nullptr);
    UFUNCTION(BlueprintPure, Category="Bookstore|Config") bool ValidateConfig(FText& Error) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Service") FText GetLastError() const { return LastResult.Message; }
    UFUNCTION(BlueprintPure, Category="Bookstore|Service") FShopCommandResult GetLastResult() const { return LastResult; }
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") TArray<FName> GetBookIds() const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool GetBookRuntime(FName BookId, FBookRuntime& Book) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool GetDecreeInfo(FName Id, FDecreeData& Decree) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool GetMarketItemInfo(FName Id, FMarketItemData& Item) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool GetEventInfo(FName Id, FEventData& Event) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool GetEndingInfo(EShopEnding Ending, FEndingData& Data) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Data") bool CanEnactDecree(FName Id, FText& Reason) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Customers") int32 GetActiveCustomerIndex() const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Customers") bool IsCurrentCustomerPresent() const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Customers") FText BuildCustomerNeedText(int32 CustomerIndex) const;
    UFUNCTION(BlueprintPure, Category="Bookstore|Inventory") bool HasMatchingStock(EBookType Type, EBookLayer Layer) const;
    UFUNCTION(BlueprintCallable, Category="Bookstore|View") bool RegisterView(UObject* View);
    UFUNCTION(BlueprintCallable, Category="Bookstore|View") void UnregisterView(UObject* View);

    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnDayChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnInventoryChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnPhaseChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnMoneyChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnCustomersChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnPollutionChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnPsychicChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnDecreeChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnCalmOpened;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnEnlightenChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnMarketChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopChanged OnCluesChanged;
    UPROPERTY(BlueprintAssignable, Category="Bookstore|Events") FShopCustomerResolved OnCustomerResolved;

private:
    FShopCatalog Catalog;
    FShopRunState State;
    FShopCommandResult LastResult;
    int32 ConfiguredSeed = -1;
    bool bConfigured = false;
    bool bCommitting = false;
    TArray<TWeakObjectPtr<UObject>> Views;

    FShopCommandResult Result(bool Success, EShopActionResult Code, const FText& Message = FText(), FName Id = NAME_None);
    FShopCommandResult Reject(EShopActionResult Code, const TCHAR* Message);
    bool ReadyForCommand();
    void Commit(FShopRunState&& Next, EShopActionResult Code = EShopActionResult::Success, FName Id = NAME_None, int32 ResolvedCustomer = INDEX_NONE, bool bNewRun = false);
    void Notify(const FShopRunState& Before, int32 ResolvedCustomer, bool bNewRun);
    bool StartDay(FShopRunState& Next, FText& Error);
    void QueueEvents(FShopRunState& Next, EShopEventTrigger Trigger, FName BookId = NAME_None);
    void DispatchModal(FShopRunState& Next);
    void CheckEnding(FShopRunState& Next, bool bFinal = false) const;
    bool SettleNight(FShopRunState& Next, FText& Error);
    bool OpenMarketInternal(FShopRunState& Next, FText& Error);
    void CompleteCustomer(FShopRunState& Next, int32 Index, EShopActionResult Code);
    bool ResolveHeavyWindow(FShopRunState& Next, FText& Error);
    void InjectPendingFakeCustomers(FShopRunState& Next);
};
