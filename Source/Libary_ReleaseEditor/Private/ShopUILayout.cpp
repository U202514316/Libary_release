#include "ShopUILayout.h"

#include "ShopUIAuthoring.h"
#include "ShopTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUILayout, Log, All);

namespace
{
    UScaleBox* Fitter(UWidgetBlueprint* Blueprint, UTextBlock* Text, EHorizontalAlignment Alignment)
    {
        const FName Name(*(Text->GetName() + TEXT("_Fit")));
        UScaleBox* Box = FindObject<UScaleBox>(Blueprint->WidgetTree, *Name.ToString());
        if (!Box) Box = Blueprint->WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), Name);
        Box->SetStretch(EStretch::ScaleToFit);
        Box->SetStretchDirection(EStretchDirection::DownOnly);
        Box->SetVisibility(ESlateVisibility::HitTestInvisible);
        Text->SetAutoWrapText(false);
        // UE5.1 has no public setter for this Designer property.
        if (FFloatProperty* Wrap = FindFProperty<FFloatProperty>(Text->GetClass(), TEXT("WrapTextAt")))
            Wrap->SetPropertyValue_InContainer(Text, 0.f);
        UScaleBoxSlot* Slot = CastChecked<UScaleBoxSlot>(Box->AddChild(Text));
        Slot->SetHorizontalAlignment(Alignment);
        Slot->SetVerticalAlignment(VAlign_Center);
        return Box;
    }

    int32 CompileOrder(const FAssetData& Asset)
    {
        if (Asset.AssetName == TEXT("WBP_UIRoot")) return 3;
        if (Asset.PackagePath.ToString().Contains(TEXT("/Components"))) return 0;
        if (Asset.AssetName == TEXT("WBP_DecreeIntroduction")) return 0;
        return Asset.AssetName == TEXT("WBP_Decree") ? 2 : 1;
    }
}

bool ShopUILayout::FitText(UWidgetBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->WidgetTree) return false;
    TArray<UWidget*> Widgets;
    Blueprint->WidgetTree->GetAllWidgets(Widgets);
    int32 Count = 0;
    for (UWidget* Widget : Widgets)
    {
        if (UButton* Button = Cast<UButton>(Widget))
        {
            // Artwork and transparent hit targets have no caption: leave those intact.
            UTextBlock* Caption = Cast<UTextBlock>(Button->GetContent());
            UScaleBox* Existing = Cast<UScaleBox>(Button->GetContent());
            if (!Caption && Existing) Caption = Cast<UTextBlock>(Existing->GetContent());
            if (!Caption) continue;
            Button->Modify(); Caption->Modify();
            Button->ClearChildren();
            if (Existing) Existing->ClearChildren();
            Caption->SetJustification(ETextJustify::Center);
            UScaleBox* Fit = Fitter(Blueprint, Caption, HAlign_Center);
            UButtonSlot* Slot = CastChecked<UButtonSlot>(Button->AddChild(Fit));
            Slot->SetHorizontalAlignment(HAlign_Fill);
            Slot->SetVerticalAlignment(VAlign_Fill);
            Slot->SetPadding(FMargin(12.f, 6.f));
            ++Count;
        }
        else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
        {
            if (!Text->GetName().StartsWith(TEXT("HUD_")) && !Text->GetName().StartsWith(TEXT("DecreeStat_"))) continue;
            UCanvasPanelSlot* Old = Cast<UCanvasPanelSlot>(Text->Slot);
            UCanvasPanel* Canvas = Old ? Cast<UCanvasPanel>(Text->GetParent()) : nullptr;
            if (!Canvas) continue; // Already fitted, or not one of the numeric HUD fields.
            const FAnchorData Layout = Old->GetLayout();
            const int32 Z = Old->GetZOrder();
            const bool AutoSize = Old->GetAutoSize();
            Text->Modify(); Canvas->RemoveChild(Text);
            UScaleBox* Fit = Fitter(Blueprint, Text, HAlign_Left);
            UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Fit);
            Slot->SetLayout(Layout); Slot->SetZOrder(Z); Slot->SetAutoSize(AutoSize);
            ++Count;
        }
    }
    UE_LOG(LogShopUILayout, Display, TEXT("Fitted %d text fields in %s"), Count, *Blueprint->GetPathName());
    return !Blueprint->WidgetTree->FindWidget(TEXT("HUD_PsychicArt")) || AlignHud(Blueprint);
}

bool ShopUILayout::RestorePsychicArtwork(UWidgetBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->WidgetTree) return false;
    UImage* Art=Cast<UImage>(Blueprint->WidgetTree->FindWidget(TEXT("HUD_PsychicArt")));
    UTextBlock* Psychic=Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(TEXT("HUD_Psychic")));
    UCanvasPanelSlot* ArtSlot=Art?Cast<UCanvasPanelSlot>(Art->Slot):nullptr;
    UScaleBox* Fit=Psychic?Cast<UScaleBox>(Psychic->GetParent()):nullptr;
    UCanvasPanelSlot* NumberSlot=Fit?Cast<UCanvasPanelSlot>(Fit->Slot):nullptr;
    UScaleBoxSlot* TextSlot=Psychic?Cast<UScaleBoxSlot>(Psychic->Slot):nullptr;
    if (!ArtSlot || !NumberSlot || !TextSlot) return false;

    // Restore the complete supplied 146x46 sprite, including its baked caption and gray bar.
    // The previous split-crop treatment removed part of the icon and changed the background.
    Art->Modify(); Psychic->Modify();
    FSlateBrush Brush=Art->Brush;
    Brush.SetUVRegion(FBox2f(FVector2f::ZeroVector,FVector2f(1.f,1.f)));
    Brush.SetImageSize(FVector2D(146,46)); Art->SetBrush(Brush);
    ArtSlot->SetPosition(FVector2D(726,3)); ArtSlot->SetSize(FVector2D(219,69));
    for (const TCHAR* Name:{TEXT("PsychicTitle"),TEXT("PsychicTitle_Fit"),TEXT("PsychicIcon")})
        if (UWidget* Extra=Blueprint->WidgetTree->FindWidget(Name)) Blueprint->WidgetTree->RemoveWidget(Extra);

    // Gray field occupies source y=2..33. Center only the value in its unobstructed right side.
    NumberSlot->SetPosition(FVector2D(837,6)); NumberSlot->SetSize(FVector2D(102,46.5));
    Psychic->SetJustification(ETextJustify::Center);
    FSlateFontInfo Font=Psychic->GetFont(); Font.Size=20; Psychic->SetFont(Font);
    TextSlot->SetHorizontalAlignment(HAlign_Center); TextSlot->SetVerticalAlignment(VAlign_Center);
    return true;
}

bool ShopUILayout::AlignHud(UWidgetBlueprint* Blueprint)
{
    if (!RestorePsychicArtwork(Blueprint)) return false;
    auto Place=[](UWidget* Widget,float X,float Y,float Width,float Height,int32 Z)
    {
        UCanvasPanelSlot* Slot=Cast<UCanvasPanelSlot>(Widget->Slot);
        Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(FVector2D::ZeroVector); Slot->SetAutoSize(false);
        Slot->SetPosition(FVector2D(X,Y)); Slot->SetSize(FVector2D(Width,Height)); Slot->SetZOrder(Z);
        Widget->SetRenderTransform(FWidgetTransform()); Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    };
    const TCHAR* Names[]={TEXT("HUD_Day"),TEXT("HUD_Money"),TEXT("HUD_Stock"),TEXT("HUD_Pollution"),TEXT("HUD_Enlighten"),TEXT("HUD_Queue")};
    for (const TCHAR* Name:Names)
    {
        UTextBlock* Text=Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(Name));
        UScaleBox* Fit=Text?Cast<UScaleBox>(Text->GetParent()):nullptr;
        UCanvasPanelSlot* Slot=Fit?Cast<UCanvasPanelSlot>(Fit->Slot):nullptr;
        if (!Slot) return false;
        const FVector2D OldPosition=Slot->GetPosition(), OldSize=Slot->GetSize();
        Place(Fit,OldPosition.X,9,OldSize.X,55,1);
        Text->SetJustification(ETextJustify::Center); Text->SetRenderTransform(FWidgetTransform());
        FSlateFontInfo Font=Text->GetFont(); Font.Size=22; Text->SetFont(Font);
        UScaleBoxSlot* TextSlot=CastChecked<UScaleBoxSlot>(Text->Slot);
        TextSlot->SetHorizontalAlignment(HAlign_Center); TextSlot->SetVerticalAlignment(VAlign_Center);
    }
    return true;
}

bool ShopUILayout::RestorePsychicProject()
{
    UWidgetBlueprint* Root=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_UIRoot.WBP_UIRoot"));
    if (!RestorePsychicArtwork(Root) || !ShopUIAuthoring::Compile(Root) || !ShopUIAuthoring::Save(Root)) return false;
    UE_LOG(LogShopUILayout,Display,TEXT("Restored the complete original psychic artwork and background; centered only its numeric value. Saved WBP_UIRoot."));
    return true;
}

bool ShopUILayout::RefineProject()
{
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/Game/ProgramA")}, true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(TEXT("/Game/ProgramA"), Assets, true);
    TArray<FAssetData> Widgets;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("UIBuild/UnlimitedPsychic");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.AssetClassPath == UWidgetBlueprint::StaticClass()->GetClassPathName() &&
            Asset.PackagePath.ToString().StartsWith(TEXT("/Game/ProgramA/UI"))) Widgets.Add(Asset);
        if (Asset.AssetClassPath != UDataTable::StaticClass()->GetClassPathName()) continue;
        UDataTable* Table = Cast<UDataTable>(Asset.GetAsset());
        if (!Table) return false;
        if (Table->GetRowStruct() == FRunRules::StaticStruct())
        {
            Table->Modify();
            for (FName Name : Table->GetRowNames())
                Table->FindRow<FRunRules>(Name, TEXT("UnlimitedPsychic"))->PsychicMax = 0;
            if (!ShopUIAuthoring::Save(Table)) return false;
            UE_LOG(LogShopUILayout, Display, TEXT("Cleared unused legacy psychic cap: %s"), *Table->GetPathName());
            if (!FFileHelper::SaveStringToFile(Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs),
                *(Directory / (Table->GetName() + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
        }
        else if (Table->GetRowStruct() == FEndingData::StaticStruct())
        {
            if (!FFileHelper::SaveStringToFile(Table->GetTableAsJSON(EDataTableExportFlags::UseJsonObjectsForStructs),
                *(Directory / (Table->GetName() + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
        }
    }
    Widgets.Sort([](const FAssetData& A, const FAssetData& B) { return CompileOrder(A) < CompileOrder(B); });
    for (const FAssetData& Asset : Widgets)
    {
        UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset.GetAsset());
        if (!FitText(Blueprint) || !ShopUIAuthoring::Compile(Blueprint) || !ShopUIAuthoring::Save(Blueprint)) return false;
    }
    UE_LOG(LogShopUILayout, Display, TEXT("Refined %d existing Widget Blueprints; preserved their named widgets, event graphs and artwork."), Widgets.Num());
    return Widgets.Num() > 0;
}
