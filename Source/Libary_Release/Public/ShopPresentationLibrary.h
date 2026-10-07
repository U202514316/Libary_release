#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateBrush.h"
#include "Components/SlateWrapperTypes.h"
#include "ShopTypes.h"
#include "ShopPresentationLibrary.generated.h"

class UUserWidget;
class UTexture2D;

UENUM(BlueprintType)
enum class EShopStatField : uint8 { Day, Money, Psychic, Pollution, Income, Expense, Queue, Stock, Enlighten };

UENUM(BlueprintType)
enum class EShopBookAction : uint8 { Sell, Buy, List, Unlist, Read, BuySecret };

/** Read-only presentation values for user-authored Widget Blueprints. Never creates or modifies widgets. */
UCLASS()
class LIBARY_RELEASE_API UShopPresentationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Returns the existing root on a local shop controller in this world; never creates one. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static UUserWidget* GetRootView(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetStatText(const UObject* WorldContextObject, EShopStatField Field);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetHudStatText(const UObject* WorldContextObject, EShopStatField Field);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static int32 GetCurrentCustomerIndex(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetCustomerName(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetCustomerNeedText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetCustomerPatienceText(const UObject* WorldContextObject);
    /** No active customer returns Normal; use GetCurrentCustomerIndex to distinguish an empty queue. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ECustomerKind GetCustomerKind(const UObject* WorldContextObject);
    /** CustomerPortraits slots: Normal 0..3 = 6128/6130/6132/6133, Hurry 4, Secret 5,
        Polluted 6..7 = 6125/6127. No present customer returns INDEX_NONE.
        Returns the portrait reserved at queue creation; UI rules prohibit repeats within a day.
        Never reads disguise flags or consumes random streams. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static int32 GetCustomerPortraitSlot(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetCustomerVisibility(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetInteractionVisibility(const UObject* WorldContextObject, bool bCustomerSelected);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetCounterStatusText(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetBookTitle(const UObject* WorldContextObject, FName BookId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetBookDescription(const UObject* WorldContextObject, FName BookId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetBookStockText(const UObject* WorldContextObject, FName BookId);
    /** Texture is an editable property on each book-card instance, separate from business data. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FSlateBrush MakeBookCoverBrush(UTexture2D* Texture);
    /** Presentation hint only. The subsequent service command remains authoritative. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool CanBookAction(const UObject* WorldContextObject, FName BookId, EShopBookAction Action);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FName GetCandidateId(const UObject* WorldContextObject, int32 Index);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeTitle(const UObject* WorldContextObject, int32 Index);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeText(const UObject* WorldContextObject, int32 Index);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeCostText(const UObject* WorldContextObject, int32 Index);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool CanChooseDecree(const UObject* WorldContextObject, int32 Index);

    /** Catalogue previews never draw candidates or enact a decree. Confirmation still uses ShopService. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeNameById(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeDescriptionById(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeSummaryById(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeAvailabilityText(const UObject* WorldContextObject, FName DecreeId, bool bCompact = false);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool CanConfirmDecree(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool HasDecreeSelection(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FLinearColor GetDecreeSelectionColor(FName DecreeId, FName SelectedDecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FLinearColor GetDecreeCardTint(const UObject* WorldContextObject, FName DecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static ESlateVisibility GetUnselectedDecreeVisibility(FName SelectedDecreeId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetEmergencyDecreeVisibility(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetDecreeEntryVisibility(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetDecreeStatusText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetBacklashLogText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetLatestBacklashText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetHistoryTitle(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetHistoryBody(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetHistoryProgress(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static bool CanVisitBlackMarket(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetBlackMarketVisibility(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetMarketBookDescription(const UObject* WorldContextObject, FName BookId);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetReadButtonText(const UObject* WorldContextObject, FName BookId);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetEndingTitle(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetEndingText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static ESlateVisibility GetEndingChoiceVisibility(const UObject* WorldContextObject, bool bChoice);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FText GetEndingConditionText(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static FLinearColor GetEndingAccent(const UObject* WorldContextObject);
    /** 0 menu, 2 shop, 3 sale, 4 day end, 5 dusk, 6 restock, 7 inside, 8 calm, 9 night end, 10 ending.
        Tutorial page 1 belongs to the Widget Blueprint and is never selected by business state. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation", meta=(WorldContext="WorldContextObject"))
    static int32 GetPhasePageIndex(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FText FormatActionResult(EShopActionResult Result);
    /** ResolveCustomer uses Rejected for a completed refusal, unlike generic command rejection. */
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FText FormatCustomerResult(EShopActionResult Result);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FText FormatCommandResult(const FShopCommandResult& Result);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FText FormatObservation(const FCustomerRuntime& Customer);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static FText GetTutorialText(int32 Index);
    UFUNCTION(BlueprintPure, Category="Bookstore|Presentation")
    static int32 GetTutorialCount();
};
