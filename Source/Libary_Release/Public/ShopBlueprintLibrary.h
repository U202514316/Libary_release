#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShopTypes.h"
#include "ShopBlueprintLibrary.generated.h"

class UShopRunSubsystem;

UCLASS()
class LIBARY_RELEASE_API UShopBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Cache the returned subsystem in the UI. It is the IShopService message target. */
    UFUNCTION(BlueprintPure, Category="Bookstore", meta=(WorldContext="WorldContextObject"))
    static UShopRunSubsystem* GetShopService(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Text") static FText GetBookTypeText(EBookType Type);
};
