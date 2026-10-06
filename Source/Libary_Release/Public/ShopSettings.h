#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "ShopSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Bookstore"))
class LIBARY_RELEASE_API UShopSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UShopSettings();
    virtual FName GetCategoryName() const override { return TEXT("Game"); }
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Books;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Customers;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> RunRules;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Decrees;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Events;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> MarketItems;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> OwlLines;
    UPROPERTY(Config, EditAnywhere, Category="Data") TSoftObjectPtr<UDataTable> Endings;
    UPROPERTY(Config, EditAnywhere, Category="Testing") int32 RandomSeed = -1;
};
