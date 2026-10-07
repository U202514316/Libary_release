#include "ShopEndingAuthoring.h"
#include "ShopUIAuthoring.h"
#include "ShopUILayout.h"
#include "ShopAudioAuthoring.h"
#include "ShopBlueprintLibrary.h"
#include "ShopTypes.h"
#include "ShopView.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Message.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopEndings, Log, All);
namespace
{
    using namespace ShopUIAuthoring;
    template<class T> T* Widget(UWidgetBlueprint* BP, const TCHAR* Name)
    { return Cast<T>(BP->WidgetTree->FindWidget(FName(Name))); }
    void SetEndingRect(UWidget* W, float X, float Y, float Width, float Height)
    {
        if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot))
        { Slot->SetPosition(FVector2D(X,Y)); Slot->SetSize(FVector2D(Width,Height)); }
    }
    bool BindVisibility(UWidgetBlueprint* BP, const TCHAR* Name, bool Choice)
    {
        for (const auto& Binding : BP->Bindings)
            if (Binding.ObjectName == Name && Binding.PropertyName == TEXT("Visibility")) return true;
        return !Bind(BP, FName(Name), TEXT("Visibility"), TEXT("GetEndingChoiceVisibility"), {{TEXT("bChoice"),Choice?TEXT("true"):TEXT("false")}}).IsEmpty();
    }
    bool WireChoice(UWidgetBlueprint* BP, const TCHAR* Button, const TCHAR* Choice)
    {
        TArray<UK2Node_ComponentBoundEvent*> Events; FBlueprintEditorUtils::GetAllNodesOfClass(BP, Events);
        for (const auto* E : Events) if (E->ComponentPropertyName == Button && E->DelegatePropertyName == TEXT("OnClicked")) return true;
        UEdGraph* Graph = EventGraph(BP);
        auto* Service = Call(Graph, UShopBlueprintLibrary::StaticClass(), TEXT("GetShopService"));
        auto* Request = Message(Graph, TEXT("RequestChooseEnding"));
        Default(Request, TEXT("Choice"), Choice);
        return Link(Self(Graph), UEdGraphSchema_K2::PN_Self, Service, TEXT("WorldContextObject")) &&
            Link(Service, UEdGraphSchema_K2::PN_ReturnValue, Request, UEdGraphSchema_K2::PN_Self) &&
            Link(ButtonEvent(BP, FName(Button)), UEdGraphSchema_K2::PN_Then, Request, UEdGraphSchema_K2::PN_Execute);
    }
}

bool ShopEndingAuthoring::Populate(UDataTable* Table)
{
    if (!Table || Table->GetRowStruct() != FEndingData::StaticStruct()) return false;
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir()/TEXT("SourceData/Endings/ending_rows.json")))) return false;
    const TArray<FString> Errors = Table->CreateTableFromJSONString(Json);
    for (const auto& Error : Errors) UE_LOG(LogShopEndings, Error, TEXT("%s"), *Error);
    return Errors.IsEmpty() && Table->GetRowNames().Num() == 5;
}

bool ShopEndingAuthoring::UpgradeWidget(UWidgetBlueprint* BP)
{
    if (!BP || !BP->WidgetTree) return false;
    UCanvasPanel* Canvas = Widget<UCanvasPanel>(BP,TEXT("Canvas_Page"));
    UTextBlock* Story = Widget<UTextBlock>(BP,TEXT("EndingStory"));
    UTextBlock* Title = Widget<UTextBlock>(BP,TEXT("EndingTitle"));
    UTextBlock* Condition = Widget<UTextBlock>(BP,TEXT("EndingCondition"));
    UButton* Restart = Widget<UButton>(BP,TEXT("BtnRestart"));
    UButton* Menu = Widget<UButton>(BP,TEXT("BtnMenu"));
    UBorder* Paper = Widget<UBorder>(BP,TEXT("EndingPaper"));
    UBorder* Accent = Widget<UBorder>(BP,TEXT("EndingAccent"));
    if (!Canvas || !Story || !Title || !Condition || !Restart || !Menu || !Paper || !Accent) return false;
    SetEndingRect(Paper,260,248,1400,725); SetEndingRect(Accent,260,248,1400,16);
    SetEndingRect(Title,326,292,1268,80); SetEndingRect(Condition,326,738,1268,92);
    if (UTextBlock* Subtitle=Widget<UTextBlock>(BP,TEXT("PageSubtitle")))
        Subtitle->SetText(FText::FromString(TEXT("每一笔交易，都在改变书店与它的主人。正文可用滚轮上下翻阅。")));
    FSlateFontInfo Font = Title->GetFont(); Font.Size=40; Title->SetFont(Font);
    Font=Condition->GetFont(); Font.Size=20; Condition->SetFont(Font);
    UScrollBox* Scroll = Widget<UScrollBox>(BP,TEXT("EndingStoryScroll"));
    if (!Scroll)
    {
        Scroll=BP->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("EndingStoryScroll"));
        Scroll->bIsVariable=true; Canvas->AddChild(Scroll); CastChecked<UCanvasPanelSlot>(Scroll->Slot)->SetZOrder(4);
        Story->RemoveFromParent(); Scroll->AddChild(Story);
    }
    SetEndingRect(Scroll,326,388,1268,330);
    Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    Scroll->SetAlwaysShowScrollbar(true);
    Scroll->SetScrollbarThickness(FVector2D(10.f,10.f));
    Font=Story->GetFont(); Font.Size=24; Story->SetFont(Font); Story->SetAutoWrapText(false);
    FindFProperty<FFloatProperty>(Story->GetClass(),TEXT("WrapTextAt"))->SetPropertyValue_InContainer(Story,1212.f);
    SetEndingRect(Restart,380,854,530,70); SetEndingRect(Menu,950,854,530,70);
    const TCHAR* Names[]={TEXT("BtnReturnTruth"),TEXT("BtnLeaveCity")};
    const TCHAR* Captions[]={TEXT("将真相归还给众人"),TEXT("赎身离开城市")};
    for (int32 Index=0;Index<2;++Index)
    {
        UButton* Button=Widget<UButton>(BP,Names[Index]);
        if (!Button)
        {
            Button=BP->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Names[Index]); Button->bIsVariable=true;
            Button->SetStyle(Restart->WidgetStyle); Button->SetBackgroundColor(Restart->BackgroundColor);
            Canvas->AddChild(Button); CastChecked<UCanvasPanelSlot>(Button->Slot)->SetZOrder(4);
            UTextBlock* Caption=BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(FString(Names[Index])+TEXT("_Caption"))));
            Caption->bIsVariable=true; Caption->SetText(FText::FromString(Captions[Index]));
            Font=Caption->GetFont(); Font.Size=24; Caption->SetFont(Font);
            Caption->SetColorAndOpacity(FSlateColor(FLinearColor(.92f,.85f,.69f,1))); Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
            Button->AddChild(Caption);
        }
        SetEndingRect(Button,Index==0?380:950,854,530,70);
        if(!BindVisibility(BP,Names[Index],true))return false;
    }
    if(!BindVisibility(BP,TEXT("BtnRestart"),false)||!BindVisibility(BP,TEXT("BtnMenu"),false)||!Compile(BP))return false;
    if(!WireChoice(BP,TEXT("BtnReturnTruth"),TEXT("ReturnTruth"))||!WireChoice(BP,TEXT("BtnLeaveCity"),TEXT("LeaveCity")))return false;
    TArray<UK2Node_CustomEvent*> CustomEvents; FBlueprintEditorUtils::GetAllNodesOfClass(BP,CustomEvents);
    bool HasReset=false; for (const auto* Event:CustomEvents) HasReset |= Event->CustomFunctionName==TEXT("ResetEnding");
    if(!HasReset)
    {
        UEdGraph* Graph=EventGraph(BP); FGraphNodeCreator<UK2Node_CustomEvent> Maker(*Graph);
        auto* Reset=Maker.CreateNode(); Reset->CustomFunctionName=TEXT("ResetEnding"); Maker.Finalize();
        auto* Top=Call(Graph,UScrollBox::StaticClass(),TEXT("ScrollToStart"));
        if(!Link(Get(Graph,TEXT("EndingStoryScroll")),TEXT("EndingStoryScroll"),Top,UEdGraphSchema_K2::PN_Self)||
            !Link(Reset,UEdGraphSchema_K2::PN_Then,Top,UEdGraphSchema_K2::PN_Execute))return false;
    }
    return ShopUILayout::FitText(BP) && Compile(BP) && ShopAudioAuthoring::WireWidget(BP) && Compile(BP);
}

bool ShopEndingAuthoring::UpgradeProject()
{
    UDataTable* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/Release/Data/DT_Endings.DT_Endings"));
    if(!Populate(Table) || !Save(Table)) return false;
    UDataTable* Rules=LoadObject<UDataTable>(nullptr,TEXT("/Game/ProgramA/UI/Data/DT_RunRules_UI.DT_RunRules_UI"));
    FRunRules* Row=Rules?Rules->FindRow<FRunRules>(TEXT("Default"),TEXT("FiveEndings")):nullptr;
    if(!Row)return false;
    Row->bReturnRequiresRedeemTarget=true; Row->RedeemTarget=1500; // Preserve the selected calendar.
    if(!Save(Rules))return false;
    UWidgetBlueprint* Ending=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_Ending.WBP_Ending"));
    if(!UpgradeWidget(Ending)||!Save(Ending))return false;
    UWidgetBlueprint* Root=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/ProgramA/UI/WBP_UIRoot.WBP_UIRoot"));
    if(!Root)return false;
    TArray<UK2Node_CallFunction*> Calls; FBlueprintEditorUtils::GetAllNodesOfClass(Root,Calls);
    bool ResetWired=false; for(const auto* CallNode:Calls)ResetWired |= CallNode->NodeComment==TEXT("FiveEndings:ResetStory");
    if(!ResetWired)
    {
        TArray<UK2Node_Event*> Events; FBlueprintEditorUtils::GetAllNodesOfClass(Root,Events);
        UK2Node_Event* Event=nullptr; for(auto* Candidate:Events)if(Candidate->EventReference.GetMemberName()==TEXT("ShowEnding"))Event=Candidate;
        if(!Event)return false;
        auto* Reset=Call(Event->GetGraph(),Ending->GeneratedClass,TEXT("ResetEnding")); Reset->NodeComment=TEXT("FiveEndings:ResetStory");
        if(!Link(Get(Event->GetGraph(),TEXT("EndingPage")),TEXT("EndingPage"),Reset,UEdGraphSchema_K2::PN_Self))return false;
        UEdGraphPin* Then=Event->FindPin(UEdGraphSchema_K2::PN_Then); const auto Old=Then->LinkedTo; Then->BreakAllPinLinks();
        if(!Link(Event,UEdGraphSchema_K2::PN_Then,Reset,UEdGraphSchema_K2::PN_Execute))return false;
        for(UEdGraphPin* Target:Old)if(!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Reset->FindPin(UEdGraphSchema_K2::PN_Then),Target))return false;
    }
    if(!Compile(Root)||!Save(Root))return false;
    UE_LOG(LogShopEndings,Display,TEXT("Five source-authored endings, explicit truth choice and scrolling narrative saved; selected calendar preserved."));
    return true;
}
