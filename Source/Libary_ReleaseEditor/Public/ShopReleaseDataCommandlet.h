#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShopReleaseDataCommandlet.generated.h"

/** Creates the reviewed Program A tables without overwriting assets. -VerifyOnly only reads. */
UCLASS()
class LIBARY_RELEASEEDITOR_API UShopReleaseDataCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UShopReleaseDataCommandlet();
    virtual int32 Main(const FString& Params) override;
};
