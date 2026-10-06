#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"

/** A queue: only the first unresolved customer can be served or lose patience. */
namespace ShopCustomers
{
    // Authored portrait slots: Normal 0..3, Hurry 4, Secret 5, Polluted 6..7.
    LIBARY_RELEASE_API int32 PortraitCapacity(ECustomerKind Kind);
    // Does not consume random streams. ExcludeIndex releases a waiting slot for a disguised replacement.
    LIBARY_RELEASE_API int32 ChoosePortrait(ECustomerKind Kind, int32 Day, int32 QueueIndex, FName TemplateId,
        const TArray<FCustomerRuntime>& ReservedCustomers, int32 ExcludeIndex = INDEX_NONE);
    // All customers visit the surface shop. Time controls the count and eligible roles.
    LIBARY_RELEASE_API bool GenerateForTime(FShopRunState& State, const FShopCatalog& Catalog, bool bNight, FText& Error);
    // Legacy mapping: Table means daytime; Inside means nighttime, not a customer location.
    LIBARY_RELEASE_API bool Generate(FShopRunState& State, const FShopCatalog& Catalog, EBookLayer Layer, FText& Error);
    LIBARY_RELEASE_API int32 Current(const FShopRunState& State);
    LIBARY_RELEASE_API bool AllServed(const FShopRunState& State);
    // Schedule the current unresolved visitor using validated rules and CosmeticRandom.
    LIBARY_RELEASE_API void ResetArrival(FShopRunState& State, const FRunRules& Rules);
    LIBARY_RELEASE_API bool Complete(FShopRunState& State, int32 Index, EShopActionResult Resolution);
    LIBARY_RELEASE_API int32 AdvancePatience(FShopRunState& State, const FRunRules& Rules, float DeltaSeconds);
    // Compatibility for callers using the default rule set; production passes its configured rules.
    LIBARY_RELEASE_API int32 AdvancePatience(FShopRunState& State, float DeltaSeconds);
    LIBARY_RELEASE_API FText BuildNeedText(const FCustomerRuntime& Customer, const FShopCatalog& Catalog);
}
