#include "ShopGameGuideAuthoring.h"
#include "ShopUIAuthoring.h"
#include "ShopAudioAuthoring.h"
#include "ShopGameGuideWidget.h"
#include "ShopPlayerController.h"
#include "ShopPresentationLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopGameGuide, Log, All);

namespace
{
    using namespace ShopUIAuthoring;
    const TCHAR* GuidePackage = TEXT("/Game/ProgramA/UI/WBP_GameGuide");
    const TCHAR* OwlPackage = TEXT("/Game/ProgramA/UI/Art/T_OwlPerched");
    const FLinearColor Ink(.055f,.065f,.075f,1), Cream(.91f,.86f,.75f,1), Gold(.46f,.30f,.11f,1);
    bool Good = true;
    template<typename T> T* Load(const FString& Path) { return FPackageName::DoesPackageExist(Path) ? LoadObject<T>(nullptr,*(Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path))) : nullptr; }
    template<typename T> T* W(UWidgetBlueprint* BP,FName Name)
    {
        T* Result=BP->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name); Result->bIsVariable=true; return Result;
    }
    void Wire(UEdGraphNode* A,FName Out,UEdGraphNode* B,FName In) { Good=Link(A,Out,B,In)&&Good; }
    void Next(UEdGraphNode* A,UEdGraphNode* B) { Wire(A,UEdGraphSchema_K2::PN_Then,B,UEdGraphSchema_K2::PN_Execute); }
    UK2Node_CallFunction* Target(UEdGraph* G,FName Widget,UClass* Class,FName Method)
    {
        auto* N=Call(G,Class,Method); Wire(Get(G,Widget),Widget,N,UEdGraphSchema_K2::PN_Self); return N;
    }
    void Place(UCanvasPanel* Canvas,UWidget* Child,float X,float Y,float Width,float Height,int32 Z=0)
    {
        auto* Slot=Canvas->AddChildToCanvas(Child); Slot->SetPosition(FVector2D(X,Y)); Slot->SetSize(FVector2D(Width,Height)); Slot->SetZOrder(Z);
    }
    UTextBlock* Text(UWidgetBlueprint* BP,FName Name,const TCHAR* Value,int32 Size,FLinearColor Color,float Wrap=0)
    {
        UTextBlock* T=W<UTextBlock>(BP,Name); T->SetText(FText::FromString(Value));
        FSlateFontInfo Font=T->GetFont(); Font.Size=Size; T->SetFont(Font); T->SetColorAndOpacity(Color);
        T->SetVisibility(ESlateVisibility::HitTestInvisible); T->SetAutoWrapText(false);
        if (FFloatProperty* P=FindFProperty<FFloatProperty>(T->GetClass(),TEXT("WrapTextAt"))) P->SetPropertyValue_InContainer(T,Wrap);
        if (FFloatProperty* P=FindFProperty<FFloatProperty>(T->GetClass(),TEXT("LineHeightPercentage"))) P->SetPropertyValue_InContainer(T,1.25f);
        return T;
    }
    UBorder* Block(UWidgetBlueprint* BP,UCanvasPanel* Canvas,FName Name,FLinearColor Color,float X,float Y,float Width,float Height)
    {
        auto* Border=W<UBorder>(BP,Name); Border->SetBrushColor(Color); Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible); Place(Canvas,Border,X,Y,Width,Height); return Border;
    }
    UButton* Button(UWidgetBlueprint* BP,FName Name,const TCHAR* Caption)
    {
        auto* B=W<UButton>(BP,Name); FButtonStyle Style;
        Style.Normal.DrawAs=Style.Hovered.DrawAs=Style.Pressed.DrawAs=ESlateBrushDrawType::Box;
        Style.Normal.TintColor=FSlateColor(FLinearColor::White); Style.Hovered.TintColor=FSlateColor(FLinearColor(1.15f,1.15f,1.15f,1)); Style.Pressed.TintColor=FSlateColor(FLinearColor(.8f,.8f,.8f,1));
        Style.NormalPadding=Style.PressedPadding=FMargin(0); B->SetStyle(Style); B->SetBackgroundColor(Gold); B->SetCursor(EMouseCursor::Hand);
        auto* Label=Text(BP,FName(*(Name.ToString()+TEXT("_Caption"))),Caption,24,Cream);
        Label->SetJustification(ETextJustify::Center); auto* Slot=CastChecked<UButtonSlot>(B->AddChild(Label));
        Slot->SetHorizontalAlignment(HAlign_Center); Slot->SetVerticalAlignment(VAlign_Center); Slot->SetPadding(FMargin(8)); return B;
    }
    UTexture2D* ImportOwl()
    {
        if (UTexture2D* Existing=Load<UTexture2D>(OwlPackage)) return Existing;
        auto* Factory=NewObject<UTextureFactory>(); Factory->UdimRegexPattern=TEXT("^$");
        const FString Source=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("SourceArt/UI/Characters/Original/角色与猫头鹰立绘/猫头鹰立绘.PNG"));
        auto* Owl=Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(),CreatePackage(OwlPackage),TEXT("T_OwlPerched"),RF_Public|RF_Standalone,*Source,nullptr,Factory));
        if (!Owl) return nullptr;
        Owl->LODGroup=TEXTUREGROUP_UI; Owl->CompressionSettings=TC_EditorIcon; Owl->MipGenSettings=TMGS_NoMipmaps;
        Owl->SRGB=true; Owl->NeverStream=true; Owl->VirtualTextureStreaming=false; Owl->PostEditChange();
        if (!Save(Owl)) return nullptr; FAssetRegistryModule::AssetCreated(Owl); return Owl;
    }
    void Card(UWidgetBlueprint* BP,UVerticalBox* Column,int32 Section)
    {
        const FString Prefix=FString::Printf(TEXT("GuideSection%d"),Section);
        UBorder* Paper=W<UBorder>(BP,FName(*Prefix)); Paper->SetBrushColor(FLinearColor(.97f,.94f,.87f,1)); Paper->SetPadding(FMargin(26,22));
        auto* CardSlot=Column->AddChildToVerticalBox(Paper); CardSlot->SetPadding(FMargin(0,0,14,18));
        auto* Body=W<UVerticalBox>(BP,FName(*(Prefix+TEXT("Column")))); Paper->SetContent(Body);
        auto* Heading=Text(BP,FName(*(Prefix+TEXT("Title"))),TEXT(""),27,Gold,1200);
        Body->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0,0,0,14));
        auto* Copy=Text(BP,FName(*(Prefix+TEXT("Body"))),TEXT(""),22,Ink,1200); Body->AddChildToVerticalBox(Copy);
        Good=!Bind(BP,Heading->GetFName(),TEXT("Text"),TEXT("GetGameGuideTitle"),{{TEXT("Section"),FString::FromInt(Section)}}).IsEmpty()&&Good;
        Good=!Bind(BP,Copy->GetFName(),TEXT("Text"),TEXT("GetGameGuideText"),{{TEXT("Section"),FString::FromInt(Section)}}).IsEmpty()&&Good;
    }
    UWidgetBlueprint* BuildGuide(UTexture2D* Owl)
    {
        if (UWidgetBlueprint* Existing=Load<UWidgetBlueprint>(GuidePackage)) return Existing;
        auto* BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UShopGameGuideWidget::StaticClass(),CreatePackage(GuidePackage),TEXT("WBP_GameGuide"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
        if (!BP) return nullptr;
        FAssetRegistryModule::AssetCreated(BP);
        auto* Backdrop=W<UBorder>(BP,TEXT("ModalBackdrop")); BP->WidgetTree->RootWidget=Backdrop; Backdrop->SetBrushColor(FLinearColor(.025f,.03f,.04f,.98f)); Backdrop->SetPadding(FMargin(0));
        auto* Scale=W<UScaleBox>(BP,TEXT("GuideScale")); Scale->SetStretch(EStretch::ScaleToFit); Backdrop->SetContent(Scale);
        auto* Size=W<USizeBox>(BP,TEXT("GuideSize")); Size->SetWidthOverride(1920); Size->SetHeightOverride(1080); Scale->SetContent(Size);
        auto* Canvas=W<UCanvasPanel>(BP,TEXT("GuideCanvas")); Size->SetContent(Canvas);
        Block(BP,Canvas,TEXT("GuideFrame"),Ink,72,60,1776,960);
        Block(BP,Canvas,TEXT("HeaderRule"),Gold,112,215,1664,3);
        Place(Canvas,Text(BP,TEXT("GuideTitle"),TEXT("夜枭手册 · 游戏介绍"),38,Cream),112,103,1350,64);
        Place(Canvas,Text(BP,TEXT("GuideSubtitle"),TEXT("结局的方向，取决于你的每一次经营与取舍。"),21,Cream),115,172,1340,36);
        Place(Canvas,Button(BP,TEXT("BtnClose"),TEXT("关闭介绍")),1576,105,200,65);
        Place(Canvas,Button(BP,TEXT("BtnEndings"),TEXT("结局条件")),112,260,254,72);
        UButton* Pollution=Button(BP,TEXT("BtnPollution"),TEXT("律令与污染")); Pollution->SetBackgroundColor(FLinearColor(.14f,.17f,.19f,1)); Place(Canvas,Pollution,112,352,254,72);
        Place(Canvas,Text(BP,TEXT("PauseNotice"),TEXT("经营已暂停\n顾客会等你读完"),21,Cream,244),119,456,244,88);
        UImage* Portrait=W<UImage>(BP,TEXT("GuideOwl")); Portrait->SetBrushFromTexture(Owl); Portrait->SetVisibility(ESlateVisibility::HitTestInvisible); Place(Canvas,Portrait,68,560,340,340);
        auto* Pages=W<UWidgetSwitcher>(BP,TEXT("GuidePages")); Place(Canvas,Pages,418,248,1358,681); Pages->SetActiveWidgetIndex(0);
        for (int32 Page=0;Page<2;++Page)
        {
            auto* Scroll=W<UScrollBox>(BP,Page==0?TEXT("EndingsScroll"):TEXT("PollutionScroll")); Scroll->SetScrollBarVisibility(ESlateVisibility::Visible); Pages->AddChild(Scroll);
            auto* Column=W<UVerticalBox>(BP,Page==0?TEXT("EndingsColumn"):TEXT("PollutionColumn")); Scroll->AddChild(Column);
            if (Page==0) { for(int32 Section=0;Section<5;++Section) Card(BP,Column,Section); Card(BP,Column,9); }
            else for(int32 Section=5;Section<9;++Section) Card(BP,Column,Section);
        }
        Place(Canvas,Text(BP,TEXT("GuideFooter"),TEXT("鼠标滚轮翻阅 · 关闭或按 Esc 返回 · 从原来的剩余时间继续"),21,Cream),422,952,1340,38);
        if (!Good || !Compile(BP)) return nullptr;
        auto* G=EventGraph(BP);
        auto* Close=Call(G,UShopGameGuideWidget::StaticClass(),TEXT("CloseGuide")); Next(ButtonEvent(BP,TEXT("BtnClose")),Close);
        for (int32 I=0;I<2;++I)
        {
            FName Active=I==0?TEXT("BtnEndings"):TEXT("BtnPollution"); FName Inactive=I==0?TEXT("BtnPollution"):TEXT("BtnEndings");
            auto* Page=Target(G,TEXT("GuidePages"),UWidgetSwitcher::StaticClass(),TEXT("SetActiveWidgetIndex")); Default(Page,TEXT("Index"),FString::FromInt(I)); Next(ButtonEvent(BP,Active),Page);
            auto* Select=Target(G,Active,UButton::StaticClass(),TEXT("SetBackgroundColor")); Default(Select,TEXT("InBackgroundColor"),TEXT("(R=0.46,G=0.30,B=0.11,A=1)")); Next(Page,Select);
            auto* Deselect=Target(G,Inactive,UButton::StaticClass(),TEXT("SetBackgroundColor")); Default(Deselect,TEXT("InBackgroundColor"),TEXT("(R=0.14,G=0.17,B=0.19,A=1)")); Next(Select,Deselect);
        }
        if (!Good || !ShopAudioAuthoring::WireWidget(BP) || !Compile(BP) || !Save(BP)) return nullptr;
        return BP;
    }
}

bool ShopGameGuideAuthoring::ApplyMailbox(UWidgetBlueprint* BP)
{
    using namespace ShopUIAuthoring;
    if (!BP || !BP->WidgetTree) return false;
    if (BP->WidgetTree->FindWidget(TEXT("BtnOwlGuide"))) return true;
    auto* Scene=Cast<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("FirstFloorScene")));
    auto* Owl=Load<UTexture2D>(OwlPackage); if (!Scene || !Owl) return false;
    // The original transparent PNG remains unchanged. The mailbox occludes the lower
    // perch/tail through a clipped UMG frame; the frame's bottom meets the mailbox cap.
    auto* Perch=W<UCanvasPanel>(BP,TEXT("MailboxOwlFrame")); Perch->SetClipping(EWidgetClipping::ClipToBounds);
    Perch->SetVisibility(ESlateVisibility::HitTestInvisible); Place(Scene,Perch,27,195,116,145,6);
    auto* Portrait=W<UImage>(BP,TEXT("MailboxOwlPortrait")); Portrait->SetBrushFromTexture(Owl); Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Perch,Portrait,-70,-50,256,256);
    auto* Hit=W<UButton>(BP,TEXT("BtnOwlGuide")); FButtonStyle Style;
    Style.Normal.DrawAs=Style.Hovered.DrawAs=Style.Pressed.DrawAs=Style.Disabled.DrawAs=ESlateBrushDrawType::NoDrawType;
    Hit->SetStyle(Style); Hit->SetCursor(EMouseCursor::Hand); Hit->SetToolTipText(FText::FromString(TEXT("点击猫头鹰 · 游戏介绍")));
    Place(Scene,Hit,27,195,116,145,7);
    if (!Compile(BP)) return false;
    auto* G=EventGraph(BP);
    auto* Host=Call(G,UShopPresentationLibrary::StaticClass(),TEXT("GetShopController")); Wire(Self(G),UEdGraphSchema_K2::PN_Self,Host,TEXT("WorldContextObject"));
    auto* Open=Call(G,AShopPlayerController::StaticClass(),TEXT("OpenGameGuide")); Wire(Host,UEdGraphSchema_K2::PN_ReturnValue,Open,UEdGraphSchema_K2::PN_Self); Next(ButtonEvent(BP,TEXT("BtnOwlGuide")),Open);
    return Good && ShopAudioAuthoring::WireWidget(BP) && Compile(BP);
}

bool ShopGameGuideAuthoring::UpgradeProject()
{
    Good=true;
    UTexture2D* Owl=ImportOwl(); if (!Owl) return false;
    UWidgetBlueprint* Guide=BuildGuide(Owl); if (!Guide) return false;
    UWidgetBlueprint* Shop=Load<UWidgetBlueprint>(TEXT("/Game/ProgramA/UI/WBP_SurfaceShop"));
    if (!ApplyMailbox(Shop) || !Save(Shop)) return false;
    UBlueprint* Controller=Load<UBlueprint>(TEXT("/Game/ProgramA/UI/Framework/BP_UIPlayerController"));
    if (!Controller || !Compile(Controller)) return false;
    auto* Defaults=Cast<AShopPlayerController>(Controller->GeneratedClass->GetDefaultObject()); if (!Defaults) return false;
    Defaults->GameGuideWidgetClass=Guide->GeneratedClass; if (!Save(Controller)) return false;
    UE_LOG(LogShopGameGuide,Display,TEXT("Saved mailbox owl, editable game guide, live rule bindings and controller guide class. Business tables and other pages unchanged."));
    return Good;
}
