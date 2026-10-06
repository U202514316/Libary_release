#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UWidgetBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node_Event;
class UK2Node_ComponentBoundEvent;
class UK2Node_CallFunction;
class UK2Node_Message;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_Self;

/** Editor-only construction helpers. All authored UI remains editable Widget Blueprint content. */
namespace ShopUIAuthoring
{
    UWidgetBlueprint* CreateWidgetAsset(const FString& PackagePath);
    bool Compile(UBlueprint* Blueprint);
    // UWorld assets are saved as .umap with PKG_ContainsMap; other assets use .uasset.
    bool Save(UObject* Asset);
    UEdGraph* EventGraph(UBlueprint* Blueprint);
    UK2Node_Event* Event(UBlueprint* Blueprint, UClass* Owner, FName EventName);
    // The Designer widget must have bIsVariable=true and the Blueprint must have been compiled once.
    UK2Node_ComponentBoundEvent* ButtonEvent(UWidgetBlueprint* Blueprint, FName ButtonName);
    UK2Node_CallFunction* Call(UEdGraph* Graph, UClass* Owner, FName FunctionName);
    UK2Node_Message* Message(UEdGraph* Graph, FName FunctionName);
    UK2Node_VariableGet* Get(UEdGraph* Graph, FName VariableName);
    UK2Node_VariableSet* Set(UEdGraph* Graph, FName VariableName);
    UK2Node_Self* Self(UEdGraph* Graph);
    bool Link(UEdGraphNode* From, FName OutputPin, UEdGraphNode* To, FName InputPin);
    void Default(UEdGraphNode* Node, FName InputPin, const FString& Value);
    void AddVariable(UBlueprint* Blueprint, FName Name, const FEdGraphPinType& Type, const FString& DefaultValue = TEXT(""));

    // Maps are helper INPUT pin name -> literal value / member variable name.
    // The helper must be a static pure ShopPresentationLibrary function. WorldContextObject is wired to self.
    // Returns the generated binding function name, or empty on failure. IsEnabled aliases bIsEnabled.
    FString Bind(UWidgetBlueprint* Blueprint, FName Widget, FName Property, FName HelperFunction,
        const TMap<FName, FString>& Constants = {}, const TMap<FName, FName>& Variables = {});
}
