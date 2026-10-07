#include "ShopAudioComponent.h"
#include "ShopAudioPalette.h"
#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"

namespace
{
    void StopVoice(UAudioComponent* Voice)
    {
        if (IsValid(Voice)) { Voice->Stop(); Voice->DestroyComponent(); }
    }
}

UShopAudioComponent::UShopAudioComponent() { PrimaryComponentTick.bCanEverTick = false; }

bool UShopAudioComponent::InitializeAudio(UShopAudioPalette* InPalette, bool bEnableOutput)
{
    if (bInitialized) return true;
    AShopPlayerController* PC = Cast<AShopPlayerController>(GetOwner());
    if (!PC || !PC->IsLocalController() || !InPalette) return false;
    Run = PC->GetShopRun();
    if (!Run) return false;
    Palette = InPalette; bOutputEnabled = bEnableOutput; bInitialized = true;
    bHasSnapshot = false; bMenuOverride = false; AnnouncedCustomers.Reset();
    EventCounts.Reset(); LastPlayTimes.Reset(); MusicStarts = 0;
    if (!Run->RegisterView(this)) { ShutdownAudio(); return false; }
    return true;
}

void UShopAudioComponent::ShutdownAudio()
{
    if (Run) Run->UnregisterView(this);
    StopVoice(MusicVoice); StopVoice(AmbienceVoice); StopVoice(FadingMusic); StopVoice(FadingAmbience);
    for (UAudioComponent* Voice : EffectVoices) StopVoice(Voice);
    EffectVoices.Reset(); MusicVoice = nullptr; AmbienceVoice = nullptr; FadingMusic = nullptr; FadingAmbience = nullptr;
    CurrentMusic = NAME_None; CurrentAmbience = NAME_None; Run = nullptr; Palette = nullptr;
    bInitialized = false; bHasSnapshot = false; bModalDucked = false;
}

void UShopAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ShutdownAudio(); Super::EndPlay(Reason);
}

float UShopAudioComponent::LoopVolume(FName Key, bool bAmbience) const
{
    const FShopAudioEntry* Entry = Palette ? Palette->Sounds.Find(Key) : nullptr;
    if (!Entry || bMuted) return 0.f;
    return Entry->Volume * Palette->MasterVolume * MasterLevel *
        (bAmbience ? Palette->AmbienceVolume * AmbienceLevel : Palette->MusicVolume * MusicLevel) *
        (bModalDucked ? Palette->ModalMusicScale : 1.f);
}

void UShopAudioComponent::SetLoop(FName Key, bool bAmbience)
{
    FName& Current = bAmbience ? CurrentAmbience : CurrentMusic;
    if (Current == Key) return;
    TObjectPtr<UAudioComponent>& Voice = bAmbience ? AmbienceVoice : MusicVoice;
    TObjectPtr<UAudioComponent>& Fading = bAmbience ? FadingAmbience : FadingMusic;
    StopVoice(Fading); Fading = Voice; Voice = nullptr;
    if (IsValid(Fading)) Fading->FadeOut(Palette->CrossfadeSeconds, 0.f);
    Current = Key;
    if (!Key.IsNone() && !bAmbience) ++MusicStarts;
    const FShopAudioEntry* Entry = Palette->Sounds.Find(Key);
    if (bOutputEnabled && Entry && Entry->Sound)
    {
        Voice = UGameplayStatics::CreateSound2D(this, Entry->Sound, 1.f, 1.f, Entry->StartTime);
        if (Voice) Voice->FadeIn(Palette->CrossfadeSeconds, LoopVolume(Key, bAmbience), Entry->StartTime);
    }
}

void UShopAudioComponent::UpdateMix()
{
    if (IsValid(MusicVoice)) MusicVoice->AdjustVolume(.2f, LoopVolume(CurrentMusic, false));
    if (IsValid(AmbienceVoice)) AmbienceVoice->AdjustVolume(.2f, LoopVolume(CurrentAmbience, true));
}

void UShopAudioComponent::SetMixLevels(float Master, float Music, float Effects, float Ambience)
{
    MasterLevel = FMath::Clamp(Master, 0.f, 1.f); MusicLevel = FMath::Clamp(Music, 0.f, 1.f);
    EffectsLevel = FMath::Clamp(Effects, 0.f, 1.f); AmbienceLevel = FMath::Clamp(Ambience, 0.f, 1.f);
    UpdateMix();
    StopVoice(FadingMusic); FadingMusic = nullptr; StopVoice(FadingAmbience); FadingAmbience = nullptr;
    for (UAudioComponent* Voice : EffectVoices) StopVoice(Voice);
    EffectVoices.Reset();
}

void UShopAudioComponent::SetMuted(bool bInMuted)
{
    bMuted = bInMuted; UpdateMix();
    if (bMuted)
    {
        StopVoice(FadingMusic); FadingMusic = nullptr; StopVoice(FadingAmbience); FadingAmbience = nullptr;
        for (UAudioComponent* Voice : EffectVoices) StopVoice(Voice); EffectVoices.Reset();
    }
}

void UShopAudioComponent::PlayEvent(FName Event)
{
    if (!bInitialized || !Palette) return;
    const FShopAudioEntry* Entry = Palette->Sounds.Find(Event);
    if (!Entry || !Entry->Sound) return;
    const double Now = FPlatformTime::Seconds();
    const double Cooldown = Event == TEXT("UI_Hover") ? .10 : Event == TEXT("UI_Click") ? .035 : 0.;
    if (const double* Last = LastPlayTimes.Find(Event)) if (Now - *Last < Cooldown) return;
    LastPlayTimes.Add(Event, Now); ++EventCounts.FindOrAdd(Event);
    if (!bOutputEnabled || bMuted) return;
    EffectVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Voice) { return !IsValid(Voice) || !Voice->IsPlaying(); });
    // Fast clicks cannot accumulate an unbounded number of overlapping sounds.
    while (EffectVoices.Num() >= 6) { StopVoice(EffectVoices[0]); EffectVoices.RemoveAt(0); }
    const float Gain = Entry->Volume * Palette->MasterVolume * MasterLevel * Palette->EffectsVolume * EffectsLevel;
    if (Gain > 0.f)
        if (UAudioComponent* Voice = UGameplayStatics::SpawnSound2D(this, Entry->Sound, Gain, 1.f, Entry->StartTime)) EffectVoices.Add(Voice);
}

void UShopAudioComponent::NotifyScreen(FName Screen)
{
    if (!bInitialized) return;
    if (Screen == TEXT("MainMenu") || Screen == TEXT("Tutorial"))
    {
        bMenuOverride = true; bModalDucked = false;
        SetLoop(TEXT("Music_Title"), false); SetLoop(TEXT("Ambience_Rain"), true); UpdateMix();
    }
}

void UShopAudioComponent::ApplyScene(const FRunSnapshot& S)
{
    const bool bDuck = S.Phase == EGamePhase::Calm || S.Phase == EGamePhase::History;
    const bool bDuckChanged = bModalDucked != bDuck;
    bModalDucked = bDuck;
    const bool bNewDayModal = bDuck && S.Day > 0 && (!bHasSnapshot || S.Day != Previous.Day || Previous.Phase == EGamePhase::Boot);
    if ((!bDuck || bNewDayModal) && !bMenuOverride)
    {
        FName Music = TEXT("Music_Title"), Ambience = TEXT("Ambience_Rain");
        switch (bNewDayModal ? EGamePhase::Day : S.Phase)
        {
        case EGamePhase::Day: case EGamePhase::Sell: case EGamePhase::DayEnd:
        case EGamePhase::NightShop: case EGamePhase::NightSell:
            Music = TEXT("Music_Shop"); Ambience = NAME_None; break;
        case EGamePhase::Restock: Music = TEXT("Music_Market"); Ambience = NAME_None; break;
        case EGamePhase::Inside: case EGamePhase::InsideSell:
            Music = TEXT("Music_Inside"); Ambience = TEXT("Ambience_Inside"); break;
        case EGamePhase::Market: Music = TEXT("Music_Inside"); break;
        case EGamePhase::End:
            Ambience = NAME_None;
            switch (S.Ending)
            {
            case EShopEnding::Closed: case EShopEnding::FailedRedemption: Music = TEXT("Music_End_Closed"); break;
            case EShopEnding::PollutionReleased: Music = TEXT("Music_End_Release"); break;
            case EShopEnding::Returned: Music = TEXT("Music_End_Return"); break;
            case EShopEnding::Cycle: case EShopEnding::Redeemed: Music = TEXT("Music_End_Cycle"); break;
            default: break;
            }
            break;
        default: break;
        }
        SetLoop(Music, false); SetLoop(Ambience, true);
    }
    if (bDuckChanged) UpdateMix();
}

void UShopAudioComponent::RefreshShop_Implementation(const FRunSnapshot& S)
{
    if (!bInitialized) return;
    const bool bNewDay = !bHasSnapshot || S.Day != Previous.Day ||
        (S.Phase != Previous.Phase && (Previous.Phase == EGamePhase::Boot || Previous.Phase == EGamePhase::End));
    if (bNewDay) { AnnouncedCustomers.Reset(); if (S.Day > 0 && S.Phase != EGamePhase::End) bMenuOverride = false; }
    ApplyScene(S);
    if (bHasSnapshot && !bMenuOverride)
    {
        if (S.Phase != Previous.Phase)
        {
            switch (S.Phase)
            {
            case EGamePhase::Restock: case EGamePhase::Market: PlayEvent(TEXT("Market_Open")); break;
            case EGamePhase::Inside:
                if (Previous.Phase != EGamePhase::Calm && Previous.Phase != EGamePhase::History) PlayEvent(TEXT("Inside_Enter"));
                break;
            case EGamePhase::DayEnd: PlayEvent(TEXT("Day_End")); break;
            case EGamePhase::Calm: PlayEvent(TEXT("Decree_Open")); break;
            case EGamePhase::End: PlayEvent(TEXT("Ending_Enter")); break;
            default: break;
            }
        }
        if (S.Phase == EGamePhase::History && (Previous.Phase != EGamePhase::History || S.PendingEventId != Previous.PendingEventId))
            PlayEvent(TEXT("History_Found"));
        if (S.Phase != EGamePhase::End && S.PollutionStage > Previous.PollutionStage)
            PlayEvent(S.PollutionStage == EPollutionStage::Heavy ? TEXT("Pollution_Heavy") :
                S.PollutionStage == EPollutionStage::Medium ? TEXT("Pollution_Medium") : TEXT("Pollution_Light"));
        for (const FDecreeRuntime& D : S.ActiveDecrees)
        {
            if (!D.bLoopholeTriggered) continue;
            const FDecreeRuntime* Old = Previous.ActiveDecrees.FindByPredicate([&](const FDecreeRuntime& P) { return P.Id == D.Id && P.EnactedTurn == D.EnactedTurn; });
            if (Old && !Old->bLoopholeTriggered) { PlayEvent(TEXT("Decree_Backlash")); break; }
        }
    }
    if (S.bCustomerPresent && !bMenuOverride && Run && (S.Phase == EGamePhase::Day || S.Phase == EGamePhase::Sell || S.Phase == EGamePhase::NightShop))
    {
        const TArray<FCustomerRuntime> Customers = Run->GetCustomers_Implementation();
        const int32 Index = Customers.IndexOfByPredicate([](const FCustomerRuntime& C) { return !C.bServed; });
        if (Index != INDEX_NONE && !AnnouncedCustomers.Contains(Index)) { AnnouncedCustomers.Add(Index); PlayEvent(TEXT("Customer_Arrive")); }
    }
    Previous = S; bHasSnapshot = true;
}

void UShopAudioComponent::ResolveCustomer_Implementation(int32 CustomerIndex, EShopActionResult Result)
{
    if (Result == EShopActionResult::Sold) PlayEvent(TEXT("Sale_Success"));
    else if (Result == EShopActionResult::Expired) PlayEvent(TEXT("Customer_Timeout"));
    else if (Result == EShopActionResult::WrongBook || Result == EShopActionResult::NoMatch) PlayEvent(TEXT("Sale_Wrong"));
}

void UShopAudioComponent::ShowDecreeResult_Implementation(FName DecreeId, const FShopCommandResult& Result)
{
    if (Result.bSucceeded) PlayEvent(TEXT("Decree_Enact"));
}

void UShopAudioComponent::ShowObserveResult_Implementation(int32 CustomerIndex, const FCustomerRuntime& Customer)
{
    if (Customer.bFake && Customer.bObserved) PlayEvent(TEXT("Fake_Revealed"));
}
