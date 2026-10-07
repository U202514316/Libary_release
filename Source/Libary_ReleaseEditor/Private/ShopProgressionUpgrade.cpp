#include "ShopProgressionUpgrade.h"
#include "ShopUIAuthoring.h"
#include "ShopUILayout.h"
#include "ShopTypes.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopProgression,Log,All);

bool ShopProgressionUpgrade::Run()
{
    UDataTable* Books=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/Release/Data/DT_Books.DT_Books"));
    UDataTable* Rules=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/UI/Data/DT_RunRules_UI.DT_RunRules_UI"));
    UDataTable* Endings=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/Release/Data/DT_Endings.DT_Endings"));
    UWidgetBlueprint* Root=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_UIRoot.WBP_UIRoot"));
    UWidgetBlueprint* Inside=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_InsideShop.WBP_InsideShop"));
    if (!Books || !Rules || !Endings || !Root || !Inside) return false;
    FRunRules* Rule=Rules->FindRow<FRunRules>(TEXT("Default"),TEXT("EnlightenHUD"));
    FEndingData* Pollution=Endings->FindRow<FEndingData>(TEXT("PollutionReleased"),TEXT("EnlightenHUD"));
    FEndingData* Closed=Endings->FindRow<FEndingData>(TEXT("Closed"),TEXT("EnlightenHUD"));
    if (!Rule || !Pollution || !Closed) return false;
    Rule->HistoryFragmentEnlightenGain=10; // Preserve the selected calendar and all unrelated user tuning.
    Pollution->Priority=1; Closed->Priority=2;
    int32 SecretCount=0;
    for (FName Id:Books->GetRowNames())
    {
        FBookData* Book=Books->FindRow<FBookData>(Id,TEXT("EnlightenHUD"));
        if (Book && Book->BookType==EBookType::Secret && Book->Layer==EBookLayer::Inside)
        { Book->SaleEnlightenChance=1.f; Book->SaleEnlightenYield=5; ++SecretCount; }
    }
    if (SecretCount!=7) return false;
    UTextBlock* Subtitle=Cast<UTextBlock>(Inside->WidgetTree->FindWidget(TEXT("PageSubtitle")));
    if (!Subtitle) return false;
    Subtitle->SetText(FText::FromString(TEXT("同本每晚翻阅一次：污染 +10、灵能 +10；30% 概率获得新残页，获得时启蒙 +10。里书成功售出启蒙 +5。")));
    if (!ShopUIAuthoring::Compile(Inside)) return false;
    if (!ShopUILayout::FitText(Root) || !ShopUIAuthoring::Compile(Root)) return false;
    for (UObject* Asset:TArray<UObject*>{Books,Rules,Endings,Inside,Root})
        if (!ShopUIAuthoring::Save(Asset)) return false;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("UIBuild/EnlightenHUD");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for (UDataTable* Table:{Books,Rules,Endings})
        if (!FFileHelper::SaveStringToFile(Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs),
            *(Directory/(Table->GetName()+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    UE_LOG(LogShopProgression,Display,TEXT("Saved: pollution-first endings, new fragment +10 enlightenment, seven secret sales +5 guaranteed, centered HUD values with complete original psychic artwork."));
    return true;
}
