#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopView.h"
#include "ShopAudioComponent.generated.h"

class UAudioComponent;
class UShopAudioPalette;
class UShopRunSubsystem;

/** Local presentation observer. Never advances the run or consumes its random stream. */
UCLASS(ClassGroup=(Bookstore), meta=(BlueprintSpawnableComponent))
class LIBARY_RELEASE_API UShopAudioComponent : public UActorComponent, public IShopView
{
    GENERATED_BODY()
public:
    UShopAudioComponent();
    bool InitializeAudio(UShopAudioPalette* InPalette, bool bEnableOutput = true);
    void ShutdownAudio();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio") void PlayEvent(FName Event);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio") void NotifyScreen(FName Screen);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio") void SetMixLevels(float Master, float Music, float Effects, float Ambience);
    UFUNCTION(BlueprintCallable, Category="Bookstore|Audio") void SetMuted(bool bInMuted);
    UPROPERTY(BlueprintReadOnly, Category="Bookstore|Audio") FName CurrentMusic;
    UPROPERTY(BlueprintReadOnly, Category="Bookstore|Audio") FName CurrentAmbience;
    UPROPERTY(BlueprintReadOnly, Category="Bookstore|Audio") bool bModalDucked = false;
    // Dispatch diagnostics also work with output disabled in the headless UI verifier.
    int32 EventCount(FName Event) const { return EventCounts.FindRef(Event); }
    int32 MusicStartCount() const { return MusicStarts; }
    bool IsAudioInitialized() const { return bInitialized; }

    virtual void RefreshShop_Implementation(const FRunSnapshot& Snapshot) override;
    virtual void ResolveCustomer_Implementation(int32 CustomerIndex, EShopActionResult Result) override;
    virtual void ShowDecreeResult_Implementation(FName DecreeId, const FShopCommandResult& Result) override;
    virtual void ShowObserveResult_Implementation(int32 CustomerIndex, const FCustomerRuntime& Customer) override;
private:
    UPROPERTY(Transient) TObjectPtr<UShopAudioPalette> Palette;
    UPROPERTY(Transient) TObjectPtr<UShopRunSubsystem> Run;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicVoice;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> AmbienceVoice;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> FadingMusic;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> FadingAmbience;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> EffectVoices;
    FRunSnapshot Previous;
    TSet<int32> AnnouncedCustomers;
    TMap<FName, int32> EventCounts;
    TMap<FName, double> LastPlayTimes;
    bool bInitialized = false, bOutputEnabled = true, bHasSnapshot = false, bMenuOverride = false, bMuted = false;
    float MasterLevel = 1.f, MusicLevel = 1.f, EffectsLevel = 1.f, AmbienceLevel = 1.f;
    int32 MusicStarts = 0;
    void ApplyScene(const FRunSnapshot& Snapshot);
    void SetLoop(FName Key, bool bAmbience);
    void UpdateMix();
    float LoopVolume(FName Key, bool bAmbience) const;
};
