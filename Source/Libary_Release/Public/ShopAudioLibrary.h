#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShopAudioLibrary.generated.h"

class UShopAudioComponent;

UCLASS()
class LIBARY_RELEASE_API UShopAudioLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Bookstore|Audio", meta=(WorldContext="WorldContextObject"))
    static UShopAudioComponent* GetShopAudio(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio", meta=(WorldContext="WorldContextObject"))
    static void PlayWidgetAction(const UObject* WorldContextObject, FName WidgetBlueprint, FName ButtonName);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio", meta=(WorldContext="WorldContextObject"))
    static void PlayWidgetHover(const UObject* WorldContextObject);
    // Placed immediately after the synchronous service message, before any other command.
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio", meta=(WorldContext="WorldContextObject"))
    static void AfterUICommand(const UObject* WorldContextObject, FName Command);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio", meta=(WorldContext="WorldContextObject"))
    static void NotifyAudioScreen(const UObject* WorldContextObject, FName Screen);
};
