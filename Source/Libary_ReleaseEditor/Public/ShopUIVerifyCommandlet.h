#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShopUIVerifyCommandlet.generated.h"

/** Loads saved editable UI assets and drives their real generated Blueprint button handlers. */
UCLASS()
class LIBARY_RELEASEEDITOR_API UShopUIVerifyCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UShopUIVerifyCommandlet();
    virtual int32 Main(const FString& Params) override;
};
