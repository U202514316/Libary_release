#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ShopTypes.h"
#include "ShopService.generated.h"

/** Implemented by UShopRunSubsystem, NOT the GameInstance object itself. */
UINTERFACE(BlueprintType)
class LIBARY_RELEASE_API UShopService : public UInterface
{
    GENERATED_BODY()
};

class LIBARY_RELEASE_API IShopService
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestNewRun();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FRunSnapshot GetSnapshot() const;
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") TArray<FCustomerRuntime> GetCustomers() const;
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool GetBookInfo(FName BookId, FBookData& BookData, int32& Stock) const;
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") EShopActionResult RequestBeginSell(int32 CustomerIndex);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") EShopActionResult RequestSell(FName BookId);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestCancelSell();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestEndDay();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestContinue();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestRestock(FName BookId);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") bool RequestNextDay();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestObserveCustomer(int32 CustomerIndex);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestRejectCustomer(int32 CustomerIndex);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestOpenInside();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestOpenRestock();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestCollectSecret(FName BookId);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestReadSecret(FName BookId);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestEndNight();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestPurify(int32 Amount);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestEnactDecree(FName DecreeId);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestSkipDecree();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestHistoryChoice(EHistoryChoice Choice);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestOwlTalk();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestOpenMarket();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestBuyMarketItem(FName Id);
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Bookstore|Service") FShopCommandResult RequestCloseMarket();
};

