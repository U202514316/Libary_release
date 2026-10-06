#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"

/** Value-model operations. The coordinator owns phase checks and commits the run copy. */
namespace ShopEconomy
{
    // Explicit legacy-fixture conversion only; live operations must never invent missing copies.
    LIBARY_RELEASE_API void NormalizeSecretCopies(FBookRuntime& Book);
    LIBARY_RELEASE_API bool IsSecretInventoryValid(const FBookRuntime& Book);
    // Recompute caches after deliberately adding/removing/updating authoritative copies.
    LIBARY_RELEASE_API void RefreshBookCounts(FBookRuntime& Book);
    LIBARY_RELEASE_API void Reset(FShopRunState& State, const FShopCatalog& Catalog);
    LIBARY_RELEASE_API bool AddMoney(FShopRunState& State, int32 Delta, FText& Error, bool bRequireFunds = false);
    LIBARY_RELEASE_API bool Restock(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error);
    LIBARY_RELEASE_API bool Collect(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error);
    LIBARY_RELEASE_API bool Read(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error);
    // Moves one owned secret copy onto/off the surface sales shelf without changing ownership.
    LIBARY_RELEASE_API bool SetSecretListing(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, bool bListed, FText& Error);
    LIBARY_RELEASE_API EShopActionResult Sell(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, const FCustomerRuntime& Customer, FText& Error);
    LIBARY_RELEASE_API bool HasMatchingStock(const FShopRunState& State, const FShopCatalog& Catalog, EBookType Type, EBookLayer Layer);
    LIBARY_RELEASE_API bool PayRent(FShopRunState& State, const FRunRules& Rules, FText& Error);
    LIBARY_RELEASE_API int32 TotalStock(const FShopRunState& State);
}
