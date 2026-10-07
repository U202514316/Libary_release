#include "ShopAudioBuildCommandlet.h"
#include "ShopAudioPalette.h"
#include "ShopAudioAuthoring.h"
#include "ShopAudioVerification.h"
#include "ShopUIAuthoring.h"
#include "ShopPlayerController.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Exporters/SoundExporterWAV.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Factories/Factory.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopAudioBuild, Log, All);

UShopAudioBuildCommandlet::UShopAudioBuildCommandlet()
{
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}

namespace
{
    const FString AudioRoot = TEXT("/Game/ProgramA/Audio/");
    template<typename T> T* Load(const FString& PackagePath)
    {
        return LoadObject<T>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath)));
    }
    template<typename T> T* Asset(const FString& PackagePath)
    {
        if (FPackageName::DoesPackageExist(PackagePath)) return Load<T>(PackagePath);
        T* Object = NewObject<T>(CreatePackage(*PackagePath), *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Object); return Object;
    }
    USoundWave* ImportWave(const FString& Stem)
    {
        const FString Path = AudioRoot + TEXT("Waves/") + Stem;
        if (FPackageName::DoesPackageExist(Path)) return Load<USoundWave>(Path);
        UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/AudioEditor.SoundFactory"));
        if (!Class) return nullptr;
        UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), Class);
        bool bCanceled = false;
        USoundWave* Wave = Cast<USoundWave>(Factory->ImportObject(USoundWave::StaticClass(), CreatePackage(*Path), FName(*Stem),
            RF_Public | RF_Standalone, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/Audio/Prepared/") / (Stem + TEXT(".wav"))), nullptr, bCanceled));
        if (!Wave || bCanceled) return nullptr;
        Wave->Volume = 1.f; Wave->bLooping = false;
        if (!ShopUIAuthoring::Save(Wave)) return nullptr;
        return Wave;
    }
    USoundCue* Loop(USoundWave* Wave)
    {
        USoundCue* Cue = Asset<USoundCue>(AudioRoot + TEXT("Cues/SC_") + Wave->GetName());
        if (!Cue) return nullptr;
        if (!Cue->FirstNode)
        {
            USoundNodeWavePlayer* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>();
            Player->SetSoundWave(Wave); Player->bLooping = true; Cue->FirstNode = Player;
            Cue->LinkGraphNodesFromSoundNodes();
        }
        Cue->VolumeMultiplier = 1.f; Cue->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
        Cue->PostEditChange();
        return ShopUIAuthoring::Save(Cue) ? Cue : nullptr;
    }
    int32 AuditExisting()
{
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Audio/Existing");
    IFileManager::Get().MakeDirectory(*Directory,true);
    IAssetRegistry& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/Game/音效")},true);
    TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game/音效"),Assets,true);
    FString Report=TEXT("Asset\tClass\tSeconds\tChannels\tRate\tSource\n"); int32 Count=0;
    for(const FAssetData& Asset:Assets)
    {
        if(USoundWave* Wave=Cast<USoundWave>(Asset.GetAsset()))
        {
            const FString Filename=Directory/(Wave->GetName()+TEXT(".wav"));
            UClass* Exporter=FindObject<UClass>(nullptr,TEXT("/Script/UnrealEd.SoundExporterWAV"));
            if(!Exporter || !UExporter::ExportToFile(Wave,NewObject<UExporter>(GetTransientPackage(),Exporter),*Filename,false,false,false))return 1;
            const FString Line=FString::Printf(TEXT("%s\t%s\t%.3f\t%d\t%d\t%s\n"),*Wave->GetPathName(),*Wave->GetClass()->GetName(),Wave->Duration,
                Wave->NumChannels,Wave->GetSampleRateForCurrentPlatform(),Wave->AssetImportData?*Wave->AssetImportData->GetFirstFilename():TEXT(""));
            Report+=Line; UE_LOG(LogShopAudioBuild,Display,TEXT("%s"),*Line); ++Count;
        }
    }
    FFileHelper::SaveStringToFile(Report,*(Directory/TEXT("Inventory.tsv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return Count>0?0:1;
}
}

int32 UShopAudioBuildCommandlet::Main(const FString& Params)
{
    if (FParse::Param(*Params,TEXT("AuditOnly"))) return AuditExisting();
    if (FParse::Param(*Params,TEXT("VerifyOnly"))) return ShopAudioVerification::Verify(FParse::Param(*Params,TEXT("RenderAudio")));
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AudioEditor"));
    TMap<FString,USoundBase*> Sounds;
    const TMap<FString,FString> Reuse = {
        {TEXT("SFX_Click"),TEXT("Button")}, {TEXT("SFX_DayEnd"),TEXT("Daily_Sum")},
        {TEXT("SFX_Bell_01"),TEXT("Doorbell")}, {TEXT("SFX_Page"),TEXT("Open_Book")},
        {TEXT("SFX_Cash"),TEXT("Sell")}, {TEXT("SFX_Error"),TEXT("Wrong")}
    };
    for (const auto& Pair : Reuse)
    {
        USoundWave* Wave = Load<USoundWave>(TEXT("/Game/音效/sfx/") + Pair.Value);
        if (!Wave) return 1; Sounds.Add(Pair.Key, Wave);
    }
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::ProjectDir()/TEXT("SourceArt/Audio/Prepared/*.wav")), true, false);
    int32 Imported = 0, Loops = 0;
    for (const FString& File : Files)
    {
        const FString Stem = FPaths::GetBaseFilename(File);
        if (Reuse.Contains(Stem) || Stem == TEXT("SFX_Bell_02") || Stem == TEXT("SFX_Owl_Hoot")) continue;
        USoundWave* Wave = ImportWave(Stem);
        if (!Wave) { UE_LOG(LogShopAudioBuild,Error,TEXT("Import failed: %s"),*Stem); return 1; }
        TArray<uint8> PCM; uint32 Rate=0; uint16 Channels=0;
        if (!Wave->GetImportedSoundWaveData(PCM,Rate,Channels) || Rate!=44100 || Channels!=2 || PCM.Num()==0 || Wave->Duration<=0.f)
        { UE_LOG(LogShopAudioBuild,Error,TEXT("Invalid decoded audio: %s"),*Stem); return 1; }
        ++Imported;
        if (Stem.StartsWith(TEXT("BGM_")) || Stem.StartsWith(TEXT("AMB_")))
        {
            USoundCue* Cue = Loop(Wave); if (!Cue) return 1; Sounds.Add(Stem,Cue); ++Loops;
        }
        else Sounds.Add(Stem,Wave);
    }
    UShopAudioPalette* Palette = Asset<UShopAudioPalette>(AudioRoot + TEXT("DA_ShopAudio"));
    if (!Palette) return 1;
    // Keep user-adjusted entries and mix levels when running this command again.
    bool bValid = true;
    auto Bind = [&](const TCHAR* Event,const TCHAR* Source,float Volume,const TCHAR* Usage,float Start=0.f)
    {
        USoundBase* Sound = Sounds.FindRef(Source);
        if (!Sound) { bValid=false; UE_LOG(LogShopAudioBuild,Error,TEXT("Missing %s"),Source); return; }
        if (!Palette->Sounds.Contains(Event))
        {
            FShopAudioEntry E; E.Sound=Sound; E.Volume=Volume; E.StartTime=Start; E.Usage=FText::FromString(Usage);
            Palette->Sounds.Add(Event,E);
        }
    };
    Bind(TEXT("Music_Title"),TEXT("BGM_Title"),.65f,TEXT("主菜单、猫头鹰引导、夜间选择与夜结"));
    Bind(TEXT("Music_Shop"),TEXT("BGM_Shop_Day"),.65f,TEXT("表书店、选书售卖、白天结算"));
    Bind(TEXT("Music_Market"),TEXT("BGM_Market"),.6f,TEXT("普通集市"));
    Bind(TEXT("Music_Inside"),TEXT("BGM_Inside"),.55f,TEXT("里书店与黑市"));
    Bind(TEXT("Music_End_Closed"),TEXT("BGM_End_Closed"),.65f,TEXT("书店关闭结局"));
    Bind(TEXT("Music_End_Cycle"),TEXT("BGM_End_Cycle"),.6f,TEXT("循环结局"));
    Bind(TEXT("Music_End_Release"),TEXT("BGM_End_Release"),.45f,TEXT("污染释放结局"));
    Bind(TEXT("Music_End_Return"),TEXT("BGM_End_Return"),.6f,TEXT("归还结局"));
    Bind(TEXT("Ambience_Rain"),TEXT("AMB_Rain"),.3f,TEXT("主菜单、夜间选择、黑市的轻环境声"));
    Bind(TEXT("Ambience_Inside"),TEXT("AMB_Inside"),2.f,TEXT("里书店环境声"));
    Bind(TEXT("UI_Click"),TEXT("SFX_Click"),.65f,TEXT("控件点击；复用项目 Button"));
    Bind(TEXT("UI_Hover"),TEXT("UI_Hover"),2.5f,TEXT("鼠标进入按钮，带节流"));
    Bind(TEXT("UI_Close"),TEXT("SFX_Close"),.45f,TEXT("关闭、返回和暂不释放"));
    Bind(TEXT("UI_Page"),TEXT("SFX_Page"),1.f,TEXT("介绍页、上架和撤回；复用 Open_Book"));
    Bind(TEXT("Owl_Advance"),TEXT("SFX_Owl_Click"),1.f,TEXT("猫头鹰新手对话翻句"));
    Bind(TEXT("Customer_Arrive"),TEXT("SFX_Bell_01"),1.35f,TEXT("顾客实际到店一次；复用 Doorbell，跳过前导静音"),1.30f);
    Bind(TEXT("Customer_Timeout"),TEXT("SFX_Patience"),.9f,TEXT("顾客耐心耗尽一次"));
    Bind(TEXT("Sale_Success"),TEXT("SFX_Cash"),1.2f,TEXT("售卖成功；复用 Sell"));
    Bind(TEXT("Sale_Wrong"),TEXT("SFX_Error"),1.1f,TEXT("错误售卖或操作失败；复用 Wrong，跳过前导静音"),1.23f);
    Bind(TEXT("Purchase_Normal"),TEXT("SFX_Market_Buy"),.8f,TEXT("集市购买普通书成功"));
    Bind(TEXT("Purchase_Secret"),TEXT("SFX_Market_Taboo"),.55f,TEXT("黑市购买里书成功"));
    Bind(TEXT("Read_Secret"),TEXT("SFX_Page"),1.f,TEXT("里书翻阅成功"));
    Bind(TEXT("Decree_Open"),TEXT("SFX_Whisper"),.25f,TEXT("打开律令选择，介绍页沿用当前音乐"));
    Bind(TEXT("Decree_Enact"),TEXT("SFX_Decree"),.38f,TEXT("律令实际生效"));
    Bind(TEXT("Decree_Backlash"),TEXT("SFX_Loophole"),.5f,TEXT("律令反噬首次发生"));
    Bind(TEXT("History_Found"),TEXT("SFX_History_Enter"),.4f,TEXT("取得残页并弹出界面"));
    Bind(TEXT("Pollution_Light"),TEXT("SFX_Pollution_01"),.5f,TEXT("污染升至轻度"));
    Bind(TEXT("Pollution_Medium"),TEXT("SFX_Pollution_02"),.5f,TEXT("污染升至中度"));
    Bind(TEXT("Pollution_Heavy"),TEXT("SFX_Pollution_03"),.5f,TEXT("污染升至重度"));
    Bind(TEXT("Market_Open"),TEXT("SFX_Market_Open"),.4f,TEXT("进入集市或黑市场景"));
    Bind(TEXT("Inside_Enter"),TEXT("SFX_Door_Inside"),.6f,TEXT("进入里书店"));
    Bind(TEXT("Day_End"),TEXT("SFX_DayEnd"),1.2f,TEXT("白天结算；复用 Daily_Sum"));
    Bind(TEXT("Ending_Enter"),TEXT("SFX_Ending"),.65f,TEXT("结局揭晓一次"));
    Bind(TEXT("Fake_Revealed"),TEXT("SFX_Whisper"),.4f,TEXT("观察确认伪装顾客"));
    if (!bValid || Imported!=26 || Loops!=10 || !ShopUIAuthoring::Save(Palette)) return 1;
    IAssetRegistry& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/Game/ProgramA/UI")},true);
    TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game/ProgramA/UI"),Assets,true);
    int32 Widgets=0;
    for (const FAssetData& Data : Assets)
    {
        if (Data.AssetClassPath != UWidgetBlueprint::StaticClass()->GetClassPathName()) continue;
        UWidgetBlueprint* BP=Cast<UWidgetBlueprint>(Data.GetAsset());
        if (!ShopAudioAuthoring::WireWidget(BP) || !ShopUIAuthoring::Compile(BP) || !ShopUIAuthoring::Save(BP)) return 1;
        ++Widgets;
    }
    UBlueprint* PC=Load<UBlueprint>(TEXT("/Game/ProgramA/UI/Framework/BP_UIPlayerController"));
    if (!PC || !ShopUIAuthoring::Compile(PC)) return 1;
    AShopPlayerController* Defaults=Cast<AShopPlayerController>(PC->GeneratedClass->GetDefaultObject());
    if (!Defaults) return 1;
    Defaults->AudioPalette=Palette;
    if (!ShopUIAuthoring::Save(PC)) return 1;
    UE_LOG(LogShopAudioBuild,Display,TEXT("AUDIO READY: %d new waves, %d loops, %d reused waves, %d routes, %d widgets; controller configured."),
        Imported,Loops,Reuse.Num(),Palette->Sounds.Num(),Widgets);
    return Widgets==19?0:1;
}
