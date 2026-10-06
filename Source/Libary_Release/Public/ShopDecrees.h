#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"

/** Value-only helpers. The caller owns the transaction and discards State on failure. */
namespace ShopEffects
{
    // Positive deltas consume the queued next-pollution bonus exactly once. Reduction
    // and zero deltas do not consume it. Hitting the limit is sticky for this run.
    LIBARY_RELEASE_API bool ChangePollution(FShopRunState& State, const FShopCatalog& Catalog,
        int32 Delta, FText& Error);
    // Amount is signed for resource effects. Income/gain multipliers are applied by the
    // calling business operation, never implicitly for a second time here.
    LIBARY_RELEASE_API bool Apply(FShopRunState& State, const FShopCatalog& Catalog,
        const TArray<FShopEffect>& Effects, FName SourceId, int32 DefaultEndTurn,
        bool bIsCost, FText& Error);
    LIBARY_RELEASE_API float Multiplier(const FShopRunState& State, EShopEffectType Type);
    LIBARY_RELEASE_API int32 Sum(const FShopRunState& State, EShopEffectType Type);
}

namespace ShopDecrees
{
    LIBARY_RELEASE_API FDecreeData GetFallbackDecree(const FRunRules& Rules);
    // PollutionLimit is terminal; it deliberately does not add a fifth display stage.
    LIBARY_RELEASE_API EPollutionStage GetStage(int32 Pollution, const FRunRules& Rules);
    LIBARY_RELEASE_API void DrawCandidates(FShopRunState& State, const FShopCatalog& Catalog);
    LIBARY_RELEASE_API bool CanEnact(const FShopRunState& State, const FShopCatalog& Catalog,
        FName Id, FText& Error);
    LIBARY_RELEASE_API bool Enact(FShopRunState& State, const FShopCatalog& Catalog,
        FName Id, FText& Error);
    // Advances one pollution settlement point: an upward stage transition or a night
    // settlement. Newly enacted decrees are not advanced by their own enactment.
    LIBARY_RELEASE_API bool TickTurn(FShopRunState& State, const FShopCatalog& Catalog,
        TArray<FName>& Triggered, FText& Error);
    // Compatibility entry point: advances once, never randomly detonates a decree.
    // The coordinator owns enablement and prevents recursive/double advancement.
    LIBARY_RELEASE_API bool TriggerStageLoophole(FShopRunState& State,
        const FShopCatalog& Catalog, FText& Error);
}
