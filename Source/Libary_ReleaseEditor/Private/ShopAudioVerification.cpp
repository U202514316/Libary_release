#include "ShopAudioVerification.h"
#include "ShopAudioPalette.h"
#include "ShopPlayerController.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "AudioMixerPlatformNonRealtime.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopAudioVerify,Log,All);

int ShopAudioVerification::Verify(bool bRender)
{
    UShopAudioPalette* Palette=LoadObject<UShopAudioPalette>(nullptr,TEXT("/Game/ProgramA/Audio/DA_ShopAudio.DA_ShopAudio"));
    UClass* PC=LoadClass<AShopPlayerController>(nullptr,TEXT("/Game/ProgramA/UI/Framework/BP_UIPlayerController.BP_UIPlayerController_C"));
    if(!Palette || Palette->Sounds.Num()!=34 || !PC || CastChecked<AShopPlayerController>(PC->GetDefaultObject())->AudioPalette!=Palette)return 1;
    TArray<FName> Keys; Palette->Sounds.GetKeys(Keys); Keys.Sort(FNameLexicalLess());
    TSet<USoundCue*> Loops; TSet<USoundWave*> Waves;
    int32 Failures=0;
    for(FName Key:Keys)
    {
        const FShopAudioEntry& Entry=Palette->Sounds[Key];
        USoundWave* Wave=Cast<USoundWave>(Entry.Sound);
        const bool bLoop=Key.ToString().StartsWith(TEXT("Music_")) || Key.ToString().StartsWith(TEXT("Ambience_"));
        if(USoundCue* Cue=Cast<USoundCue>(Entry.Sound))
        {
            USoundNodeWavePlayer* Player=Cast<USoundNodeWavePlayer>(Cue->FirstNode);
            if(!bLoop || !Player || !Player->bLooping || Cue->VirtualizationMode!=EVirtualizationMode::PlayWhenSilent) { ++Failures; continue; }
            Wave=Player->GetSoundWave(); Loops.Add(Cue);
        }
        else if(bLoop) { ++Failures; continue; }
        TArray<uint8> PCM; uint32 Rate=0; uint16 Channels=0; int32 Peak=0;
        if(Wave && Wave->GetImportedSoundWaveData(PCM,Rate,Channels))
            for(int32 i=0;i+1<PCM.Num();i+=2)Peak=FMath::Max(Peak,FMath::Abs(static_cast<int32>(*reinterpret_cast<const int16*>(PCM.GetData()+i))));
        if(!Wave || Rate!=44100 || Channels!=2 || Peak==0 || Entry.StartTime>=Wave->Duration || Entry.Volume<0.f)
        { UE_LOG(LogShopAudioVerify,Error,TEXT("Invalid route: %s"),*Key.ToString()); ++Failures; }
        else Waves.Add(Wave);
    }
    if(Waves.Num()!=32 || Loops.Num()!=10)++Failures;
    UE_LOG(LogShopAudioVerify,Display,TEXT("ASSETS routes=%d uniqueAudibleWaves=%d loopCues=%d failures=%d"),Keys.Num(),Waves.Num(),Loops.Num(),Failures);
    if(Failures || !bRender)return Failures?1:0;
    // NonRealtimeAudioRenderer mixes to memory only. No hardware device or native window is used.
    if(!FParse::Param(FCommandLine::Get(),TEXT("DeterministicAudio")) || !IsAllowCommandletAudio() || !GEngine->UseSound())
    { UE_LOG(LogShopAudioVerify,Error,TEXT("Render requires -AllowCommandletAudio -DeterministicAudio.")); return 1; }
    FAudioDeviceManager* Manager=GEngine->GetAudioDeviceManager();
    FAudioDeviceHandle Device=Manager->GetMainAudioDeviceHandle();
    if(!Device.IsValid() || !Device->IsAudioMixerEnabled())return 1;
    Audio::FMixerDevice* Mixer=static_cast<Audio::FMixerDevice*>(Device.GetAudioDevice());
    if(Mixer->GetAudioMixerPlatform()->GetPlatformApi()!=TEXT("NonRealtime"))return 1;
    Audio::FMixerPlatformNonRealtime* Platform=static_cast<Audio::FMixerPlatformNonRealtime*>(Mixer->GetAudioMixerPlatform());
    IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick"))->Set(0);
    // Offline rendering can run hundreds of times faster than disk streaming. Wait for
    // requested chunks in this test process instead of racing past their load completion.
    IConsoleManager::Get().FindConsoleVariable(TEXT("au.streamcaching.ForceBlockForLoad"))->Set(1);
    FApp::SetUnfocusedVolumeMultiplier(1.f);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("ShopAudioVerificationWorld"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    // CreateWorld already initializes its persistent level and WorldSettings.
    World->bAllowAudioPlayback=true;
    Manager->SetAudioDevice(*World,Device.GetDeviceID()); Device->SetDeviceMuted(false); Device->SetTransientPrimaryVolume(1.f);
    for(USoundWave* Wave:Waves)Device->Precache(Wave,true,true,true);
    auto Flush=[](){ FAudioCommandFence Fence; Fence.BeginFence(); Fence.Wait(); };
    auto Render=[&](float Seconds)
    {
        const int32 Steps=FMath::CeilToInt(Seconds*60.f);
        for(int32 Step=0;Step<Steps;++Step)
        {
            FApp::SetDeltaTime(1./60.); FApp::SetCurrentTime(FApp::GetCurrentTime()+1./60.);
            Device->Update(true); Flush(); Platform->RenderAudio(1./60.);
        }
    };
    Render(.1f);
    FString Report=TEXT("Route\tSeconds\tSamples\tPeak\tRMS\tTailPeak\tPassed\n");
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Audio/EngineRender");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for(FName Key:Keys)
    {
        const FShopAudioEntry& Entry=Palette->Sounds[Key];
        const bool bMusic=Key.ToString().StartsWith(TEXT("Music_")),bAmbient=Key.ToString().StartsWith(TEXT("Ambience_"));
        // Title is rendered beyond its source length to exercise an actual second loop.
        const float Seconds=Key==TEXT("Music_Title")?41.f:(bMusic||bAmbient)?4.f:FMath::Min(6.f,Entry.Sound->GetDuration()-Entry.StartTime+.2f);
        const float Gain=Entry.Volume*Palette->MasterVolume*(bMusic?Palette->MusicVolume:bAmbient?Palette->AmbienceVolume:Palette->EffectsVolume);
        Mixer->StartRecording(nullptr,Seconds); Flush();
        UAudioComponent* Voice=UGameplayStatics::CreateSound2D(World,Entry.Sound,1.f,1.f,Entry.StartTime,nullptr,false,false);
        if(!Voice){++Failures;break;}
        if(bMusic||bAmbient)Voice->FadeIn(Palette->CrossfadeSeconds,Gain,Entry.StartTime); else { Voice->SetVolumeMultiplier(Gain);Voice->Play(Entry.StartTime); }
        Render(Seconds);
        float Channels=0.f,Rate=0.f;
        const Audio::FAlignedFloatBuffer& Data=Mixer->StopRecording(nullptr,Channels,Rate);
        double Squares=0.;float Peak=0.f,TailPeak=0.f;
        TArray<int16> Samples; Samples.Reserve(Data.Num());
        for(int32 Index=0;Index<Data.Num();++Index)
        {
            const float Value=Data[Index]; Squares+=Value*Value; Peak=FMath::Max(Peak,FMath::Abs(Value));
            if(Index>Data.Num()-static_cast<int32>(Rate*Channels))TailPeak=FMath::Max(TailPeak,FMath::Abs(Value));
            Samples.Add(static_cast<int16>(FMath::RoundToInt(FMath::Clamp(Value,-1.f,1.f)*32767.f)));
        }
        const bool Pass=Data.Num()>0 && Peak>.00001f && Peak<1.f && (Key!=TEXT("Music_Title") || TailPeak>.00001f);
        if(!Pass)++Failures;
        const FString Line=FString::Printf(TEXT("%s\t%.2f\t%d\t%.7f\t%.7f\t%.7f\t%d\n"),*Key.ToString(),Seconds,Data.Num(),Peak,
            Data.Num()?FMath::Sqrt(Squares/Data.Num()):0.,TailPeak,Pass);
        Report+=Line; UE_LOG(LogShopAudioVerify,Display,TEXT("RENDER %s"),*Line);
        TArray<uint8> Wav; SerializeWaveFile(Wav,reinterpret_cast<const uint8*>(Samples.GetData()),Samples.Num()*sizeof(int16),static_cast<int32>(Channels),static_cast<int32>(Rate));
        if(!FFileHelper::SaveArrayToFile(Wav,*(Directory/(Key.ToString()+TEXT(".wav")))))++Failures;
        Voice->Stop();Voice->DestroyComponent();Render(.1f);
    }
    FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("Report.tsv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    UE_LOG(LogShopAudioVerify,Display,TEXT("ENGINE AUDIO RESULT routes=%d failures=%d; no physical listening assertion."),Keys.Num(),Failures);
    return Failures?1:0;
}
