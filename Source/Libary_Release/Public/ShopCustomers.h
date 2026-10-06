#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"

/** A queue: only the first unresolved customer can be served or lose patience. */
namespace ShopCustomers
{
    LIBARY_RELEASE_API bool Generate(FShopRunState& State, const FShopCatalog& Catalog, EBookLayer Layer, FText& Error);
    LIBARY_RELEASE_API int32 Current(const FShopRunState& State);
    LIBARY_RELEASE_API bool AllServed(const FShopRunState& State);
    LIBARY_RELEASE_API bool Complete(FShopRunState& State, int32 Index, EShopActionResult Resolution);
    LIBARY_RELEASE_API int32 AdvancePatience(FShopRunState& State, const FRunRules& Rules, float DeltaSeconds);
    // Compatibility for callers using the default rule set; production passes its configured rules.
    LIBARY_RELEASE_API int32 AdvancePatience(FShopRunState& State, float DeltaSeconds);
    LIBARY_RELEASE_API FText BuildNeedText(const FCustomerRuntime& Customer, const FShopCatalog& Catalog);
}
