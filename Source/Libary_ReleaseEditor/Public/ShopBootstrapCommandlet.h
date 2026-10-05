#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShopBootstrapCommandlet.generated.h"

/** Creates explicitly incomplete prototype tables; -VerifyOnly never writes assets. */
UCLASS()
class UShopBootstrapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UShopBootstrapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
