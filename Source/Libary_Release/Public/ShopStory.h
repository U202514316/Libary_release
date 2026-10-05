#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"

/** Authored story content and weekly market operations on the coordinator's state copy. */
namespace ShopStory
{
    // Enqueue only. The coordinator promotes events and manages modal phases.
    LIBARY_RELEASE_API void QueueEvents(FShopRunState& State, const FShopCatalog& Catalog, EShopEventTrigger Trigger, FName BookId = NAME_None);
    LIBARY_RELEASE_API bool Witness(FShopRunState& State, const FShopCatalog& Catalog, EHistoryChoice Choice, FText& Error);
    LIBARY_RELEASE_API bool OpenMarket(FShopRunState& State, const FShopCatalog& Catalog, FText& Error);
    LIBARY_RELEASE_API bool BuyMarketItem(FShopRunState& State, const FShopCatalog& Catalog, FName ItemId, FText& Error);
    LIBARY_RELEASE_API bool OwlTalk(FShopRunState& State, const FShopCatalog& Catalog, FName& OutId, FText& OutText, FText& Error);
}
