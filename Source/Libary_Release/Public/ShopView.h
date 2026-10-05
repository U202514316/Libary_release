#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShopTypes.h"
#include "ShopView.generated.h"

/** Implement on the UI root, then RegisterView with the run subsystem. */
UINTERFACE(BlueprintType)
class LIBARY_RELEASE_API UShopView : public UInterface
{
    GENERATED_BODY()
};
class LIBARY_RELEASE_API IShopView
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void RefreshShop(const FRunSnapshot& Snapshot);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ResolveCustomer(int32 CustomerIndex, EShopActionResult Result);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void NewDay(int32 Day);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void OpenCalmPanel(const TArray<FName>& Candidates, const FRunSnapshot& Snapshot);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ShowDecreeResult(FName DecreeId, const FShopCommandResult& Result);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ShowObserveResult(int32 CustomerIndex, const FCustomerRuntime& Customer);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ShowHistoryPanel(FName EventId, const FEventData& Event);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ShowEnding(EShopEnding Ending, const FRunSnapshot& Snapshot);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void ShowOwlTip(FName LineId, const FText& Text);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|View") void OpenMarket(const TArray<FName>& ItemIds, const FRunSnapshot& Snapshot);
};

