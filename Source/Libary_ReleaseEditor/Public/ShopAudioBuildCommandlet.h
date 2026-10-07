#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShopAudioBuildCommandlet.generated.h"

/** Audits supplied audio and adds sound hooks to saved UMG graphs without rebuilding their layouts. */
UCLASS()
class LIBARY_RELEASEEDITOR_API UShopAudioBuildCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UShopAudioBuildCommandlet();
    virtual int32 Main(const FString& Params) override;
};
