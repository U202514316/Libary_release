#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShopUIBuildCommandlet.generated.h"

/** Authors editable UMG Widget Blueprints and K2 graphs under /Game/ProgramA/UI. */
UCLASS()
class LIBARY_RELEASEEDITOR_API UShopUIBuildCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UShopUIBuildCommandlet();
    virtual int32 Main(const FString& Params) override;
};
