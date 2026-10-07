#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShopAudioPalette.generated.h"

class USoundBase;

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FShopAudioEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="4")) float Volume = 1.f;
    // Skips source-file silence without altering the original imported asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) float StartTime = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Usage;
};

/** A single editable collection of scene loops and event sounds. No gameplay rules live here. */
UCLASS(BlueprintType)
class LIBARY_RELEASE_API UShopAudioPalette : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0", ClampMax="1")) float MasterVolume = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0", ClampMax="1")) float MusicVolume = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0", ClampMax="1")) float EffectsVolume = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0", ClampMax="1")) float AmbienceVolume = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0.05", ClampMax="3")) float CrossfadeSeconds = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mix", meta=(ClampMin="0", ClampMax="1")) float ModalMusicScale = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sounds") TMap<FName, FShopAudioEntry> Sounds;
};
