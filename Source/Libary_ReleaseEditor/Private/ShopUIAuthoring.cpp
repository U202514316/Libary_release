#include "ShopUIAuthoring.h"

#include "ShopService.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Message.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUIAuthoring, Log, All);

namespace
{
    const UEdGraphSchema_K2* Schema() { return GetDefault<UEdGraphSchema_K2>(); }

    void Position(UEdGraphNode* Node)
    {
        const int32 Index = FMath::Max(0, Node->GetGraph()->Nodes.Num() - 1);
        Node->NodePosX = (Index % 4) * 400;
        Node->NodePosY = (Index / 4) * 240;
        Node->SetFlags(RF_Transactional);
    }

    template<typename T, typename Configure>
    T* NewNode(UEdGraph* Graph, Configure ConfigureNode)
    {
        if (!Graph)
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Cannot create %s without a graph."), *T::StaticClass()->GetName());
            return nullptr;
        }
        FGraphNodeCreator<T> Creator(*Graph);
        T* Node = Creator.CreateNode(false);
        ConfigureNode(Node);
        Creator.Finalize();
        Position(Node);
        return Node;
    }

    UFunction* FindFunction(UClass* Owner, FName Name)
    {
        UFunction* Function = Owner ? Owner->FindFunctionByName(Name) : nullptr;
        if (!Function) UE_LOG(LogShopUIAuthoring, Error, TEXT("Missing function %s.%s."), *GetNameSafe(Owner), *Name.ToString());
        return Function;
    }

    bool SetDefault(UEdGraphNode* Node, FName Name, const FString& Value)
    {
        UEdGraphPin* Pin = Node ? Node->FindPin(Name, EGPD_Input) : nullptr;
        if (!Pin)
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Missing default input %s.%s."), *GetNameSafe(Node), *Name.ToString());
            return false;
        }
        if (!Pin->LinkedTo.IsEmpty())
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Cannot set a literal on linked input %s.%s."), *GetNameSafe(Node), *Name.ToString());
            return false;
        }
        FString ParsedValue;
        TObjectPtr<UObject> ParsedObject = nullptr;
        FText ParsedText;
        const bool bText = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text;
        if (bText) ParsedText = FText::FromString(Value);
        else Schema()->GetPinDefaultValuesFromString(Pin->PinType, Node, Value, ParsedValue, ParsedObject, ParsedText, false);
        // TrySetDefaultValue silently keeps the old literal if parsing fails, so validate
        // the requested value first instead of accidentally accepting the old default.
        const FString Error = Schema()->IsPinDefaultValid(Pin, ParsedValue, ParsedObject.Get(), ParsedText);
        if (!Error.IsEmpty())
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Invalid default %s.%s: %s"), *GetNameSafe(Node), *Name.ToString(), *Error);
            return false;
        }
        if (bText) Schema()->TrySetDefaultText(*Pin, ParsedText);
        else Schema()->TrySetDefaultValue(*Pin, Value);
        return true;
    }
}

UWidgetBlueprint* ShopUIAuthoring::CreateWidgetAsset(const FString& PackagePath)
{
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("UMGEditor"));
    if (!FPackageName::IsValidLongPackageName(PackagePath))
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Invalid Widget Blueprint package path: %s"), *PackagePath);
        return nullptr;
    }
    const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
    const FString ObjectPath = PackagePath + TEXT(".") + Name;
    UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
    if (!Existing && FPackageName::DoesPackageExist(PackagePath)) Existing = LoadObject<UObject>(nullptr, *ObjectPath);
    if (Existing)
    {
        UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Existing);
        if (!Blueprint) UE_LOG(LogShopUIAuthoring, Error, TEXT("Existing asset is not a Widget Blueprint: %s"), *ObjectPath);
        return Blueprint;
    }
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package) return nullptr;
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), TEXT("ShopUIAuthoring")));
    if (!Blueprint || !Blueprint->WidgetTree)
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Failed to create Widget Blueprint: %s"), *PackagePath);
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    Blueprint->MarkPackageDirty();
    return Blueprint;
}

bool ShopUIAuthoring::Compile(UBlueprint* Blueprint)
{
    if (!Blueprint) return false;
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("UMGEditor"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
    for (const auto& Message : Results.Messages)
    {
        if (Message->GetSeverity() == EMessageSeverity::Error)
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("%s: %s"), *Blueprint->GetPathName(), *Message->ToText().ToString());
        }
        else if (Message->GetSeverity() == EMessageSeverity::Warning || Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
        {
            UE_LOG(LogShopUIAuthoring, Warning, TEXT("%s: %s"), *Blueprint->GetPathName(), *Message->ToText().ToString());
        }
    }
    const bool bSucceeded = Results.NumErrors == 0 && Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr;
    UE_LOG(LogShopUIAuthoring, Display, TEXT("Compile %s: %s (%d errors, %d warnings)"),
        *Blueprint->GetPathName(), bSucceeded ? TEXT("success") : TEXT("FAILED"), Results.NumErrors, Results.NumWarnings);
    return bSucceeded;
}

bool ShopUIAuthoring::Save(UObject* Asset)
{
    if (!Asset) return false;
    UPackage* Package = Asset->GetOutermost();
    if (!Package || Package == GetTransientPackage() || !FPackageName::IsValidLongPackageName(Package->GetName()))
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Cannot save transient or invalid asset %s."), *GetPathNameSafe(Asset));
        return false;
    }
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        if (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error || Blueprint->Status == BS_Dirty)
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Compile the Blueprint before saving %s."), *Asset->GetPathName());
            return false;
        }
    }
    const bool bMap = Asset->IsA<UWorld>();
    if (bMap) Package->SetPackageFlags(PKG_ContainsMap);
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),
        bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    Package->GetMetaData();
    Asset->SetFlags(RF_Public | RF_Standalone);
    Asset->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
    if (!bSaved) UE_LOG(LogShopUIAuthoring, Error, TEXT("Failed to save %s."), *Filename);
    return bSaved;
}

UEdGraph* ShopUIAuthoring::EventGraph(UBlueprint* Blueprint)
{
    if (!Blueprint) return nullptr;
    if (UEdGraph* Existing = FBlueprintEditorUtils::FindEventGraph(Blueprint)) return Existing;
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, UEdGraphSchema_K2::GN_EventGraph,
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
    return Graph;
}

UK2Node_Event* ShopUIAuthoring::Event(UBlueprint* Blueprint, UClass* Owner, FName EventName)
{
    if (!Blueprint || !FindFunction(Owner, EventName)) return nullptr;
    if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, Owner, EventName))
    {
        if (Existing->IsAutomaticallyPlacedGhostNode()) Existing->NodeComment.Empty();
        Existing->SetEnabledState(ENodeEnabledState::Enabled);
        return Existing;
    }
    return NewNode<UK2Node_Event>(EventGraph(Blueprint), [Owner, EventName](UK2Node_Event* Node)
    {
        Node->EventReference.SetExternalMember(EventName, Owner);
        Node->bOverrideFunction = true;
        Node->SetEnabledState(ENodeEnabledState::Enabled);
    });
}

UK2Node_ComponentBoundEvent* ShopUIAuthoring::ButtonEvent(UWidgetBlueprint* Blueprint, FName ButtonName)
{
    if (!Blueprint || !Blueprint->WidgetTree) return nullptr;
    UButton* Button = Cast<UButton>(Blueprint->WidgetTree->FindWidget(ButtonName));
    FObjectProperty* Property = Blueprint->SkeletonGeneratedClass
        ? FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, ButtonName) : nullptr;
    if (!Button || !Property)
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Button %s must exist, be a Designer variable, and be compiled before binding in %s."),
            *ButtonName.ToString(), *Blueprint->GetPathName());
        return nullptr;
    }
    TArray<UK2Node_ComponentBoundEvent*> Existing;
    FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Existing);
    for (UK2Node_ComponentBoundEvent* Node : Existing)
        if (Node->ComponentPropertyName == ButtonName && Node->DelegatePropertyName == TEXT("OnClicked")) return Node;
    FMulticastDelegateProperty* Delegate = FindFProperty<FMulticastDelegateProperty>(Button->GetClass(), TEXT("OnClicked"));
    if (!Delegate) return nullptr;
    return NewNode<UK2Node_ComponentBoundEvent>(EventGraph(Blueprint), [Property, Delegate](UK2Node_ComponentBoundEvent* Node)
    { Node->InitializeComponentBoundEventParams(Property, Delegate); });
}

UK2Node_CallFunction* ShopUIAuthoring::Call(UEdGraph* Graph, UClass* Owner, FName FunctionName)
{
    UFunction* Function = FindFunction(Owner, FunctionName);
    if (!Function) return nullptr;
    return NewNode<UK2Node_CallFunction>(Graph, [Function](UK2Node_CallFunction* Node) { Node->SetFromFunction(Function); });
}

UK2Node_Message* ShopUIAuthoring::Message(UEdGraph* Graph, FName FunctionName)
{
    if (!FindFunction(UShopService::StaticClass(), FunctionName)) return nullptr;
    return NewNode<UK2Node_Message>(Graph, [FunctionName](UK2Node_Message* Node)
    { Node->FunctionReference.SetExternalMember(FunctionName, UShopService::StaticClass()); });
}

UK2Node_VariableGet* ShopUIAuthoring::Get(UEdGraph* Graph, FName VariableName)
{
    return NewNode<UK2Node_VariableGet>(Graph, [VariableName](UK2Node_VariableGet* Node)
    { Node->VariableReference.SetSelfMember(VariableName); });
}

UK2Node_VariableSet* ShopUIAuthoring::Set(UEdGraph* Graph, FName VariableName)
{
    return NewNode<UK2Node_VariableSet>(Graph, [VariableName](UK2Node_VariableSet* Node)
    { Node->VariableReference.SetSelfMember(VariableName); });
}

UK2Node_Self* ShopUIAuthoring::Self(UEdGraph* Graph)
{
    return NewNode<UK2Node_Self>(Graph, [](UK2Node_Self*) {});
}

bool ShopUIAuthoring::Link(UEdGraphNode* From, FName OutputPin, UEdGraphNode* To, FName InputPin)
{
    UEdGraphPin* Output = From ? From->FindPin(OutputPin, EGPD_Output) : nullptr;
    UEdGraphPin* Input = To ? To->FindPin(InputPin, EGPD_Input) : nullptr;
    if (!Output || !Input || From->GetGraph() != To->GetGraph())
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Missing/incompatible graph pins: %s.%s -> %s.%s"),
            *GetNameSafe(From), *OutputPin.ToString(), *GetNameSafe(To), *InputPin.ToString());
        return false;
    }
    if (Output->LinkedTo.Contains(Input)) return true;
    if (!Schema()->TryCreateConnection(Output, Input))
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Schema rejected connection: %s.%s -> %s.%s"),
            *From->GetName(), *OutputPin.ToString(), *To->GetName(), *InputPin.ToString());
        return false;
    }
    return true;
}

void ShopUIAuthoring::Default(UEdGraphNode* Node, FName InputPin, const FString& Value)
{
    SetDefault(Node, InputPin, Value);
}

void ShopUIAuthoring::AddVariable(UBlueprint* Blueprint, FName Name, const FEdGraphPinType& Type, const FString& DefaultValue)
{
    if (!Blueprint || Name.IsNone()) return;
    for (FBPVariableDescription& Existing : Blueprint->NewVariables)
    {
        if (Existing.VarName != Name) continue;
        if (Existing.VarType != Type)
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Existing variable %s.%s has a different type."), *Blueprint->GetPathName(), *Name.ToString());
            return;
        }
        Existing.DefaultValue = DefaultValue;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return;
    }
    if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue))
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Failed to add variable %s.%s."), *Blueprint->GetPathName(), *Name.ToString());
}

FString ShopUIAuthoring::Bind(UWidgetBlueprint* Blueprint, FName WidgetName, FName PropertyName, FName HelperFunction,
    const TMap<FName, FString>& Constants, const TMap<FName, FName>& Variables)
{
    if (!Blueprint || !Blueprint->WidgetTree) return FString();
    UWidget* Widget = Blueprint->WidgetTree->FindWidget(WidgetName);
    UClass* Library = FindObject<UClass>(nullptr, TEXT("/Script/Libary_Release.ShopPresentationLibrary"));
    UFunction* Helper = FindFunction(Library, HelperFunction);
    if (!Widget || !Helper || !Helper->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintPure))
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Binding requires an existing widget and a static pure helper: %s.%s -> %s."),
            *Blueprint->GetPathName(), *WidgetName.ToString(), *HelperFunction.ToString());
        return FString();
    }
    // UWidget's Designer label omits the bool prefix, while its delegate does not.
    if (PropertyName == TEXT("IsEnabled")) PropertyName = TEXT("bIsEnabled");
    FDelegateProperty* Delegate = FindFProperty<FDelegateProperty>(Widget->GetClass(), FName(*(PropertyName.ToString() + TEXT("Delegate"))));
    FProperty* Return = Helper->GetReturnProperty();
    FProperty* DestinationReturn = Delegate && Delegate->SignatureFunction ? Delegate->SignatureFunction->GetReturnProperty() : nullptr;
    FEdGraphPinType ReturnType, DestinationType;
    if (!Return || !DestinationReturn || !Schema()->ConvertPropertyToPinType(Return, ReturnType) ||
        !Schema()->ConvertPropertyToPinType(DestinationReturn, DestinationType) || ReturnType != DestinationType)
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("No compatible bindable property %s.%s for helper %s."),
            *WidgetName.ToString(), *PropertyName.ToString(), *HelperFunction.ToString());
        return FString();
    }
    for (const auto& Pair : Constants)
    {
        if (Variables.Contains(Pair.Key) || Pair.Key == TEXT("WorldContextObject"))
        {
            UE_LOG(LogShopUIAuthoring, Error, TEXT("Duplicate or reserved binding input %s."), *Pair.Key.ToString());
            return FString();
        }
    }
    if (Variables.Contains(TEXT("WorldContextObject")))
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("WorldContextObject is automatically bound to self."));
        return FString();
    }

    const FName FunctionName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint,
        FString::Printf(TEXT("Get_%s_%s"), *WidgetName.ToString(), *PropertyName.ToString()));
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName,
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, Delegate->SignatureFunction);
    TArray<UK2Node_FunctionEntry*> Entries;
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Entries); Graph->GetNodesOfClass(Results);
    auto FailBinding = [&]() -> FString
    {
        UE_LOG(LogShopUIAuthoring, Error, TEXT("Failed to author binding %s.%s -> %s."),
            *WidgetName.ToString(), *PropertyName.ToString(), *HelperFunction.ToString());
        FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
        return FString();
    };
    if (Entries.Num() != 1 || Results.Num() != 1) return FailBinding();
    UK2Node_FunctionEntry* Entry = Entries[0];
    UK2Node_FunctionResult* Result = Results[0];
    Entry->AddExtraFlags(FUNC_BlueprintPure | FUNC_Const);
    UK2Node_CallFunction* Function = Call(Graph, Library, HelperFunction);
    if (!Function || !Link(Entry, UEdGraphSchema_K2::PN_Then, Result, UEdGraphSchema_K2::PN_Execute) ||
        !Link(Function, UEdGraphSchema_K2::PN_ReturnValue, Result, DestinationReturn->GetFName())) return FailBinding();
    Entry->NodePosX = 0; Entry->NodePosY = 0;
    Function->NodePosX = 440; Function->NodePosY = 160;
    Result->NodePosX = 880; Result->NodePosY = 0;
    if (Function->FindPin(TEXT("WorldContextObject"), EGPD_Input))
    {
        UK2Node_Self* Context = Self(Graph);
        if (!Link(Context, UEdGraphSchema_K2::PN_Self, Function, TEXT("WorldContextObject"))) return FailBinding();
        Context->NodePosX = 0; Context->NodePosY = 160;
    }
    for (const auto& Pair : Constants)
        if (!SetDefault(Function, Pair.Key, Pair.Value)) return FailBinding();
    int32 VariableY = 300;
    for (const auto& Pair : Variables)
    {
        UK2Node_VariableGet* Variable = Get(Graph, Pair.Value);
        if (!Link(Variable, Pair.Value, Function, Pair.Key)) return FailBinding();
        Variable->NodePosX = 0; Variable->NodePosY = VariableY; VariableY += 160;
    }
    Widget->bIsVariable = true;
    FDelegateEditorBinding Binding;
    Binding.ObjectName = WidgetName.ToString();
    Binding.PropertyName = PropertyName;
    Binding.FunctionName = FunctionName;
    Binding.MemberGuid = Graph->GraphGuid;
    Binding.Kind = EBindingKind::Function;
    if (Blueprint->GeneratedClass) Binding.SourcePath.Segments.Add(FEditorPropertyPathSegment(Graph));
    Blueprint->Bindings.Remove(Binding);
    Blueprint->Bindings.Add(Binding);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return FunctionName.ToString();
}
