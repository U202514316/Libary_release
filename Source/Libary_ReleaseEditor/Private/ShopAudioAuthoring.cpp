#include "ShopAudioAuthoring.h"
#include "ShopAudioLibrary.h"
#include "ShopUIAuthoring.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Message.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
    using namespace ShopUIAuthoring;
    bool Insert(UWidgetBlueprint* BP, UEdGraphNode* Source, FName Method, const FString& Id, const TMap<FName,FString>& Defaults)
    {
        const FString Tag = TEXT("ShopAudioHook:v1:") + Id;
        TArray<UK2Node_CallFunction*> Calls; FBlueprintEditorUtils::GetAllNodesOfClass(BP, Calls);
        for (UK2Node_CallFunction* Node : Calls) if (Node->NodeComment == Tag) return true;
        UEdGraphPin* Then = Source ? Source->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
        if (!Then) return false;
        UK2Node_CallFunction* Hook = Call(Source->GetGraph(), UShopAudioLibrary::StaticClass(), Method);
        if (!Hook) return false;
        Hook->NodeComment = Tag; Hook->bCommentBubbleVisible = true;
        Hook->NodePosX = Source->NodePosX + 320; Hook->NodePosY = Source->NodePosY + 100;
        if (!Link(Self(Source->GetGraph()), UEdGraphSchema_K2::PN_Self, Hook, TEXT("WorldContextObject"))) return false;
        for (const auto& Pair : Defaults) Default(Hook, Pair.Key, Pair.Value);
        const TArray<UEdGraphPin*> OldLinks = Then->LinkedTo;
        Then->BreakAllPinLinks();
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        if (!Schema->TryCreateConnection(Then, Hook->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
        for (UEdGraphPin* Target : OldLinks)
            if (!Schema->TryCreateConnection(Hook->FindPin(UEdGraphSchema_K2::PN_Then), Target)) return false;
        return true;
    }
}

bool ShopAudioAuthoring::WireWidget(UWidgetBlueprint* BP)
{
    if (!BP || !BP->WidgetTree) return false;
    TArray<UK2Node_Message*> Messages; FBlueprintEditorUtils::GetAllNodesOfClass(BP, Messages);
    for (UK2Node_Message* Node : Messages)
    {
        const FName Method = Node->FunctionReference.GetMemberName();
        if (Method.ToString().StartsWith(TEXT("Request")) &&
            !Insert(BP, Node, TEXT("AfterUICommand"), TEXT("Command:") + Node->NodeGuid.ToString(), {{TEXT("Command"),Method.ToString()}})) return false;
    }
    TArray<UK2Node_ComponentBoundEvent*> Events; FBlueprintEditorUtils::GetAllNodesOfClass(BP, Events);
    for (UK2Node_ComponentBoundEvent* Event : Events)
    {
        if (Event->DelegatePropertyName != TEXT("OnClicked")) continue;
        const FName ButtonName = Event->ComponentPropertyName;
        if (!Insert(BP, Event, TEXT("PlayWidgetAction"), TEXT("Click:") + ButtonName.ToString(),
            {{TEXT("WidgetBlueprint"),BP->GetName()}, {TEXT("ButtonName"),ButtonName.ToString()}})) return false;
        UK2Node_ComponentBoundEvent* Hover = nullptr;
        for (UK2Node_ComponentBoundEvent* Existing : Events)
            if (Existing->ComponentPropertyName == ButtonName && Existing->DelegatePropertyName == TEXT("OnHovered")) Hover = Existing;
        if (!Hover)
        {
            FObjectProperty* Property = FindFProperty<FObjectProperty>(BP->SkeletonGeneratedClass, ButtonName);
            FMulticastDelegateProperty* Delegate = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), TEXT("OnHovered"));
            if (!Property || !Delegate) return false;
            FGraphNodeCreator<UK2Node_ComponentBoundEvent> Creator(*Event->GetGraph());
            Hover = Creator.CreateNode(false); Hover->InitializeComponentBoundEventParams(Property, Delegate); Creator.Finalize();
            Hover->NodePosX = Event->NodePosX; Hover->NodePosY = Event->NodePosY + 280;
        }
        if (!Insert(BP, Hover, TEXT("PlayWidgetHover"), TEXT("Hover:") + ButtonName.ToString(), {})) return false;
    }
    TArray<UK2Node_Event*> FlowEvents; FBlueprintEditorUtils::GetAllNodesOfClass(BP, FlowEvents);
    for (UK2Node_Event* Event : FlowEvents)
    {
        const FName Name = Event->EventReference.GetMemberName();
        if (Name != TEXT("ShowTutorial") && Name != TEXT("ShowMainMenu")) continue;
        if (!Insert(BP, Event, TEXT("NotifyAudioScreen"), TEXT("Screen:") + Name.ToString(),
            {{TEXT("Screen"),Name == TEXT("ShowTutorial") ? TEXT("Tutorial") : TEXT("MainMenu")}})) return false;
    }
    return true;
}
