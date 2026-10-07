#include "ShopMainMenuAuthoring.h"
#include "ShopUIAuthoring.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopMainMenu,Log,All);

namespace
{
    const FString ArtRoot=TEXT("/Game/ProgramA/UI/Art/MainMenu/");
    UTexture2D* Texture(const TCHAR* Name)
    {
        return LoadObject<UTexture2D>(nullptr,*(ArtRoot+Name+TEXT(".")+Name));
    }
    bool Import(const TCHAR* Name,const TCHAR* File)
    {
        if (FPackageName::DoesPackageExist(ArtRoot+Name)) return Texture(Name)!=nullptr;
        UTextureFactory* Factory=NewObject<UTextureFactory>();
        Factory->UdimRegexPattern=TEXT("^$");
        const FString Source=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("SourceArt/UI/MainMenu/Original")/File);
        UTexture2D* Art=Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(),CreatePackage(*(ArtRoot+Name)),Name,RF_Public|RF_Standalone,*Source,nullptr,Factory));
        if (!Art) return false;
        Art->LODGroup=TEXTUREGROUP_UI; Art->CompressionSettings=TC_EditorIcon;
        Art->MipGenSettings=TMGS_NoMipmaps; Art->SRGB=true; Art->NeverStream=true; Art->VirtualTextureStreaming=false;
        Art->PostEditChange();
        if (!ShopUIAuthoring::Save(Art)) return false;
        FAssetRegistryModule::AssetCreated(Art);
        return true;
    }
    void ButtonArt(UButton* Button,UTexture2D* Art,const FVector2D& Position,const FVector2D& Size)
    {
        Button->Modify(); Button->ClearChildren();
        // The complete composition already contains the normal button pixels. Keeping the
        // normal brush transparent avoids doubling the antialiased edges of the supplied art.
        FButtonStyle Style;
        Style.Normal.DrawAs=ESlateBrushDrawType::NoDrawType;
        Style.Hovered.SetResourceObject(Art); Style.Hovered.DrawAs=ESlateBrushDrawType::Image; Style.Hovered.ImageSize=Size;
        Style.Pressed=Style.Hovered; Style.Pressed.TintColor=FSlateColor(FLinearColor(.8f,.8f,.8f,1.f));
        Style.Disabled.DrawAs=ESlateBrushDrawType::NoDrawType;
        Style.NormalPadding=Style.PressedPadding=FMargin(0);
        Button->SetStyle(Style); Button->SetBackgroundColor(FLinearColor::White);
        Button->SetVisibility(ESlateVisibility::Visible); Button->SetIsEnabled(true); Button->SetCursor(EMouseCursor::Hand);
        UCanvasPanelSlot* Slot=CastChecked<UCanvasPanelSlot>(Button->Slot);
        Slot->SetPosition(Position); Slot->SetSize(Size); Slot->SetZOrder(5);
    }
}

bool ShopMainMenuAuthoring::Apply(UWidgetBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->WidgetTree) return false;
    UCanvasPanel* Canvas=Cast<UCanvasPanel>(Blueprint->WidgetTree->RootWidget);
    UImage* Background=Cast<UImage>(Blueprint->WidgetTree->FindWidget(TEXT("Background")));
    UButton* Start=Cast<UButton>(Blueprint->WidgetTree->FindWidget(TEXT("BtnStart")));
    UButton* Quit=Cast<UButton>(Blueprint->WidgetTree->FindWidget(TEXT("BtnQuit")));
    UTexture2D* Composition=Texture(TEXT("T_MainMenuComposition"));
    UTexture2D* StartArt=Texture(TEXT("T_MainMenuStart"));
    UTexture2D* QuitArt=Texture(TEXT("T_MainMenuQuit"));
    if (!Canvas || !Background || !Start || !Quit || !Composition || !StartArt || !QuitArt ||
        !Cast<UCanvasPanelSlot>(Background->Slot) || !Cast<UCanvasPanelSlot>(Start->Slot) || !Cast<UCanvasPanelSlot>(Quit->Slot)) return false;
    // Retain all old widget identities/bindings, but remove the generated overlays from view.
    for (UWidget* Child:Canvas->GetAllChildren())
        if (Child!=Background && Child!=Start && Child!=Quit) Child->SetVisibility(ESlateVisibility::Collapsed);
    Background->Modify(); Background->SetBrushFromTexture(Composition);
    Background->SetColorAndOpacity(FLinearColor::White); Background->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* Slot=CastChecked<UCanvasPanelSlot>(Background->Slot);
    Slot->SetPosition(FVector2D::ZeroVector); Slot->SetSize(FVector2D(1920,1080)); Slot->SetZOrder(0);
    // Positions measured against the original composition using the supplied button PNGs.
    ButtonArt(Start,StartArt,FVector2D(1283,369),FVector2D(317,112));
    ButtonArt(Quit,QuitArt,FVector2D(1359,593),FVector2D(317,113));
    return true;
}

bool ShopMainMenuAuthoring::UpgradeProject()
{
    const TCHAR* Names[]={TEXT("T_MainMenuComposition"),TEXT("T_MainMenuStart"),TEXT("T_MainMenuQuit"),TEXT("T_MainMenuCharacter"),TEXT("T_MainMenuLogo")};
    const TCHAR* Files[]={TEXT("画板 2.png"),TEXT("开始.png"),TEXT("退出.png"),TEXT("矩形 26.png"),TEXT("组 14.png")};
    for (int32 I=0;I<UE_ARRAY_COUNT(Names);++I) if (!Import(Names[I],Files[I])) return false;
    UWidgetBlueprint* Menu=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_MainMenu.WBP_MainMenu"));
    if (!Apply(Menu) || !ShopUIAuthoring::Compile(Menu) || !ShopUIAuthoring::Save(Menu)) return false;
    UE_LOG(LogShopMainMenu,Display,TEXT("Saved original 1920x1080 menu composition and exact Start/Quit hit areas. Existing Blueprint events, root, gameplay and audio preserved."));
    return true;
}
