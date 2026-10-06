#include "ShopUIVerifyCommandlet.h"
#include "ShopUIPreview.h"

#include "ShopGameMode.h"
#include "ShopPlayerController.h"
#include "ShopPresentationLibrary.h"
#include "ShopRunSubsystem.h"
#include "ShopView.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EditorFramework/AssetImportData.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUIVerify, Log, All);

namespace
{
    const FString AssetRoot = TEXT("/Game/ProgramA/UI/");
    const TCHAR* WidgetAssets[] = {
        TEXT("Components/WBP_SaleBookCard"), TEXT("Components/WBP_MerchantBookCard"), TEXT("Components/WBP_SecretBookCard"),
        TEXT("WBP_MainMenu"), TEXT("WBP_OwlTutorial"), TEXT("WBP_SurfaceShop"), TEXT("WBP_Bookshelf"), TEXT("WBP_DaySettlement"),
        TEXT("WBP_NightChoice"), TEXT("WBP_Merchant"), TEXT("WBP_InsideShop"), TEXT("WBP_Decree"),
        TEXT("WBP_NightSettlement"), TEXT("WBP_Ending"), TEXT("WBP_UIRoot")
    };

    struct FReport
    {
        int32 Checks = 0, Failures = 0;
        FString Lines;
        bool Check(bool bPass, const FString& Label)
        {
            ++Checks;
            Lines += (bPass ? TEXT("PASS ") : TEXT("FAIL ")) + Label + LINE_TERMINATOR;
            if (!bPass)
            {
                ++Failures;
                UE_LOG(LogShopUIVerify, Error, TEXT("%s"), *Label);
            }
            return bPass;
        }
        void Note(const FString& Text)
        {
            Lines += Text + LINE_TERMINATOR;
            UE_LOG(LogShopUIVerify, Display, TEXT("%s"), *Text);
        }
    };

    FString ObjectPath(const FString& PackagePath)
    {
        return PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath);
    }

    template<typename T> T* Load(const FString& PackagePath)
    {
        return LoadObject<T>(nullptr, *ObjectPath(PackagePath));
    }

    template<typename T> T* Child(UUserWidget* Parent, FName Name)
    {
        return Parent && Parent->WidgetTree ? Cast<T>(Parent->WidgetTree->FindWidget(Name)) : nullptr;
    }

    bool Compiled(FReport& Report, UBlueprint* Blueprint, const FString& Path)
    {
        if (!Report.Check(Blueprint != nullptr, TEXT("Load ") + Path)) return false;
        return Report.Check(Blueprint->GeneratedClass != nullptr &&
            (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings), TEXT("Saved compiled class ") + Path);
    }

    bool VerifyAuthoredEndings(FReport& Report)
    {
        UDataTable* Table = Load<UDataTable>(TEXT("/Game/ProgramA/Release/Data/DT_Endings"));
        if (!Report.Check(Table && Table->GetRowStruct() == FEndingData::StaticStruct() && Table->GetRowMap().Num() == 4,
            TEXT("Saved DT_Endings has exactly four authored FEndingData rows"))) return false;
        const FEndingData* Closed = Table->FindRow<FEndingData>(TEXT("Closed"), TEXT("UIVerify"));
        const FEndingData* Pollution = Table->FindRow<FEndingData>(TEXT("PollutionReleased"), TEXT("UIVerify"));
        const FEndingData* Returned = Table->FindRow<FEndingData>(TEXT("Returned"), TEXT("UIVerify"));
        const FEndingData* Cycle = Table->FindRow<FEndingData>(TEXT("Cycle"), TEXT("UIVerify"));
        return Report.Check(Closed && Closed->Ending == EShopEnding::Closed && Closed->Condition == EShopEndingCondition::NegativeBalance &&
                Closed->Priority == 1 && Closed->NegativeDaysRequired == 3 && !Closed->Title.IsEmpty() && !Closed->Text.IsEmpty(), TEXT("Saved Closed: priority1, three negative days, authored title/story")) &&
            Report.Check(Pollution && Pollution->Ending == EShopEnding::PollutionReleased && Pollution->Condition == EShopEndingCondition::PollutionLimit &&
                Pollution->Priority == 2 && Pollution->PollutionThreshold == 100 && !Pollution->Title.IsEmpty() && !Pollution->Text.IsEmpty(), TEXT("Saved PollutionReleased: priority2, pollution100, authored title/story")) &&
            Report.Check(Returned && Returned->Ending == EShopEnding::Returned && Returned->Condition == EShopEndingCondition::FinalThresholds &&
                Returned->Priority == 3 && Returned->MinEnlighten == 60 && Returned->MaxPollutionExclusive == 60 && !Returned->bRequireMoney &&
                !Returned->Title.IsEmpty() && !Returned->Text.IsEmpty(), TEXT("Saved Returned: priority3, final enlightenment60 and pollution below60, no money requirement")) &&
            Report.Check(Cycle && Cycle->Ending == EShopEnding::Cycle && Cycle->Condition == EShopEndingCondition::FinalFallback &&
                Cycle->Priority == 4 && !Cycle->Title.IsEmpty() && !Cycle->Text.IsEmpty(), TEXT("Saved Cycle: priority4, final fallback, authored title/story"));
    }

    struct FSession
    {
        TStrongObjectPtr<UGameInstance> Instance;
        TStrongObjectPtr<UUserWidget> Root;
        TArray<TStrongObjectPtr<UDataTable>> FixtureTables;
        UWorld* World = nullptr;
        AShopPlayerController* Controller = nullptr;
        UShopRunSubsystem* Run = nullptr;
        UWidgetSwitcher* Pages = nullptr;
        FString PreviewPrefix;
        int32 PreviewIndex = 0;
        bool bCapturePreviews = true;
        bool bCaptureSceneStates = false;
        bool bCapturedSceneStates = false;
        bool bCapturedMerchantStates = false;
        FRunRules SavedRules;
        FReport& Report;

        explicit FSession(FReport& InReport) : Instance(nullptr), Root(nullptr), Report(InReport) {}
        ~FSession()
        {
            if (Run && Root.IsValid()) Run->UnregisterView(Root.Get());
            if (Controller) Controller->RootWidget = nullptr;
            if (Root.IsValid()) Root->ReleaseSlateResources(true);
            Root.Reset();
            if (Instance.IsValid()) Instance->Shutdown();
            if (World)
            {
                if (GEngine) GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
            Instance.Reset();
        }

        bool Start(UClass* ControllerClass, UClass* RootClass, bool bInsideBranch, bool bShortScenario = false,
            int32 Seed = 731, bool bPollutionBoundary = false, bool bPollutedPortraitFixture = false)
        {
            PreviewPrefix = bPollutionBoundary ? TEXT("pollution_boundary") : bShortScenario ? TEXT("short_fixture") : bInsideBranch ? TEXT("inside") : TEXT("merchant");
            if (bPollutedPortraitFixture) PreviewPrefix = TEXT("polluted_fixture");
            bCaptureSceneStates = bCapturePreviews && !bInsideBranch && !bShortScenario && !bPollutionBoundary;
            Instance.Reset(NewObject<UGameInstance>(GEngine));
            Instance->InitializeStandalone(bInsideBranch ? TEXT("UIVerifyInside") : TEXT("UIVerifyMerchant"));
            World = Instance->GetWorld();
            if (!Report.Check(World && World->IsGameWorld(), TEXT("Create isolated real game world"))) return false;
            // Required for spawned controllers to run PostInitializeComponents and join the world's controller list.
            // BeginPlay is intentionally left to the real host in normal play, where it attaches the viewport.
            World->InitializeActorsForPlay(FURL());
            Report.Note(FString::Printf(TEXT("WORLD fixture=%s path=%s game=%d actorsInitialized=%d instanceWorld=%s"),
                *PreviewPrefix, *World->GetPathName(), World->IsGameWorld(), World->AreActorsInitialized(), *GetPathNameSafe(Instance->GetWorld())));
            Run = Instance->GetSubsystem<UShopRunSubsystem>();
            if (!Report.Check(Run != nullptr, TEXT("GameInstance owns the real ShopRunSubsystem"))) return false;

            // The two main branches use the exact saved UI rules and customer rows.
            // An explicitly labelled third scenario duplicates only data, never runtime business state.
            const FString Release = TEXT("/Game/ProgramA/Release/Data/");
            UDataTable* Books = Load<UDataTable>(Release + TEXT("DT_Books"));
            UDataTable* Customers = Load<UDataTable>(Release + TEXT("DT_Customers"));
            UDataTable* Rules = Load<UDataTable>(AssetRoot + TEXT("Data/DT_RunRules_UI"));
            UDataTable* Decrees = Load<UDataTable>(AssetRoot + TEXT("Data/DT_Decrees_UI"));
            if (!Report.Check(Rules && Rules->GetRowStruct() == FRunRules::StaticStruct() && Rules->GetRowNames().Num() == 1,
                TEXT("Saved UI rules contain one typed row"))) return false;
            SavedRules = *Rules->FindRow<FRunRules>(Rules->GetRowNames()[0], TEXT("UIVerify"));
            if (!Report.Check(SavedRules.MaxDays == 35 && SavedRules.bUseCustomerArrivalDelay && SavedRules.bUniqueDailyCustomerPortraits &&
                FMath::IsNearlyEqual(SavedRules.CustomerArrivalMin, 2.f) && FMath::IsNearlyEqual(SavedRules.CustomerArrivalMax, 4.f),
                TEXT("Actual saved UI rules: 35 days, unique daily portraits, customer arrival enabled at 2..4 seconds"))) return false;
            Report.Note(FString::Printf(TEXT("SPECIAL CUSTOMERS saved rules: MediumThreshold=%d; PollutionOnPollutedCustomer=%d; PollutionOnSell=%d"),
                SavedRules.MediumThreshold, SavedRules.PollutionOnPollutedCustomer, SavedRules.PollutionOnSell));
            if (bShortScenario || bPollutionBoundary || bPollutedPortraitFixture)
            {
                if (!Report.Check(Rules && Customers && Rules->GetRowNames().Num() == 1, TEXT("Short-fixture source tables exist"))) return false;
                Rules = DuplicateObject<UDataTable>(Rules, GetTransientPackage());
                Customers = DuplicateObject<UDataTable>(Customers, GetTransientPackage());
                FixtureTables.Emplace(Rules); FixtureTables.Emplace(Customers);
                FRunRules* Row = Rules->FindRow<FRunRules>(Rules->GetRowNames()[0], TEXT("UIVerify"));
                if (!Report.Check(Row != nullptr, TEXT("Short-fixture rule row has the expected type"))) return false;
                if (bPollutedPortraitFixture)
                {
                    Row->StartPollution = Row->MediumThreshold;
                }
                else if (bShortScenario)
                {
                    Row->MaxDays = 2;
                    Row->StartPollution = Row->LightThreshold - Row->PollutionOnSell + Row->PollutionDecay;
                    if (!Report.Check(Row->StartPollution >= 0 && Row->StartPollution < Row->LightThreshold, TEXT("Short-fixture starts below the first pollution threshold"))) return false;
                }
                else
                {
                    // A boundary fixture changes configuration, never the subsystem's runtime state.
                    Row->StartPollution = 0;
                    Row->PollutionOnSell = Row->PollutionLimit;
                }
                for (FName Id : Customers->GetRowNames())
                    if (FCustomerData* Customer = Customers->FindRow<FCustomerData>(Id, TEXT("UIVerify")))
                        if (Customer->Kind == (bPollutedPortraitFixture ? ECustomerKind::Polluted : ECustomerKind::Secret)) Customer->SpawnWeight = 1.e12f;
                Report.Note(bPollutedPortraitFixture
                    ? TEXT("TRANSIENT POLLUTED PORTRAIT FIXTURE ONLY: initial pollution=MediumThreshold; Polluted weight=1e12. Uses real customer generation, observation/sale/refusal buttons and authored textures; saved tables unchanged.")
                    : bShortScenario
                    ? TEXT("TRANSIENT SHORT FIXTURE ONLY: MaxDays=2; StartPollution=LightThreshold-PollutionOnSell+PollutionDecay; Secret weight=1e12. Saved UI assets remain unchanged.")
                    : TEXT("TRANSIENT POLLUTION BOUNDARY ONLY: saved MaxDays=35; StartPollution=0; PollutionOnSell=PollutionLimit; Secret weight=1e12. Saved endings, books and assets remain unchanged."));
            }
            if (!Report.Check(Run->ConfigureTables(Books, Customers, Rules, Decrees,
                Load<UDataTable>(Release + TEXT("DT_Events")), Load<UDataTable>(Release + TEXT("DT_MarketItems")),
                Load<UDataTable>(Release + TEXT("DT_Owl")), Seed, Load<UDataTable>(Release + TEXT("DT_Endings"))),
                TEXT("Configure saved UI tables: ") + Run->GetLastError().ToString())) return false;
            Report.Note(FString::Printf(TEXT("SESSION seed=%d scenario=%s savedMaxDays=%d"), Seed, *PreviewPrefix, SavedRules.MaxDays));
            FString Error;
            ULocalPlayer* Player = Instance->CreateLocalPlayer(0, Error, false);
            if (!Report.Check(Player != nullptr, TEXT("Create local player: ") + Error)) return false;
            Controller = World->SpawnActor<AShopPlayerController>(ControllerClass);
            if (!Report.Check(Controller != nullptr, TEXT("Spawn actual BP_UIPlayerController"))) return false;
            Controller->SetPlayer(Player);
            if (!Report.Check(Controller->IsLocalPlayerController(), TEXT("Controller has a local-player context"))) return false;
            Root.Reset(CreateWidget<UUserWidget>(Controller, RootClass, TEXT("UIVerifyRoot")));
            if (!Report.Check(Root.IsValid() && Root->WidgetTree, TEXT("Create and initialize actual WBP_UIRoot"))) return false;
            // Headless equivalent of the host's ownership/registration, without AddToViewport.
            // Business requests below are exclusively the saved Widget Blueprint button graphs.
            Controller->RootWidget = Root.Get();
            TArray<FString> ControllerDiagnostics;
            bool bControllerRegistered = false;
            for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            {
                const APlayerController* Entry = It->Get();
                bControllerRegistered |= Entry == Controller;
                const AShopPlayerController* ShopController = Cast<AShopPlayerController>(Entry);
                ControllerDiagnostics.Add(FString::Printf(TEXT("%s(local=%d,root=%s)"), *GetPathNameSafe(Entry),
                    Entry ? Entry->IsLocalController() : false, ShopController ? *GetPathNameSafe(ShopController->RootWidget.Get()) : TEXT("not-ShopPlayerController")));
            }
            Report.Note(FString::Printf(TEXT("CONTEXT controllerWorld=%s widgetWorld=%s playerWorld=%s owningPC=%s controllers=%d [%s]"),
                *GetPathNameSafe(Controller->GetWorld()), *GetPathNameSafe(Root->GetWorld()), *GetPathNameSafe(Player->GetWorld()),
                *GetPathNameSafe(Root->GetOwningPlayer()), ControllerDiagnostics.Num(), *FString::Join(ControllerDiagnostics, TEXT("; "))));
            if (!Report.Check(Controller->GetWorld() == World && Root->GetWorld() == World && Root->GetOwningPlayer() == Controller,
                TEXT("Controller, root and local player resolve the fixture world")) ||
                !Report.Check(bControllerRegistered, TEXT("Spawned controller was registered by normal actor initialization"))) return false;
            if (!Report.Check(Run->RegisterView(Root.Get()), TEXT("Register actual root ShopView"))) return false;
            Pages = Child<UWidgetSwitcher>(Root.Get(), TEXT("Pages"));
            if (!Report.Check(Pages && Pages->GetChildrenCount() == 11, TEXT("Runtime root contains all eleven real page instances"))) return false;
            return Report.Check(UShopPresentationLibrary::GetRootView(Root.Get()) == Root.Get(), TEXT("Nested UI can resolve its actual root through the local controller"));
        }

        UUserWidget* Page(const TCHAR* Name) const { return Child<UUserWidget>(Root.Get(), FName(Name)); }
        FRunSnapshot Snapshot() const { return Run->GetSnapshot_Implementation(); }
        bool At(EGamePhase Phase, int32 PageIndex, const FString& Label)
        {
            const FRunSnapshot S = Snapshot();
            const bool bPass = Report.Check(S.Phase == Phase && Pages->GetActiveWidgetIndex() == PageIndex,
                FString::Printf(TEXT("%s: phase=%d page=%d"), *Label, static_cast<int32>(S.Phase), Pages->GetActiveWidgetIndex()));
            if (bPass)
            {
                if (!VerifyHudValues()) return false;
                if ((Phase == EGamePhase::Day || Phase == EGamePhase::Sell) && !VerifyCustomerPortraitSelection()) return false;
                const FString Name = FString::Printf(TEXT("%s_%02d_page%d"), *PreviewPrefix, PreviewIndex++, PageIndex);
                // Rendering uses the real current page; the verifier never changes the switcher for a screenshot.
                if (bCapturePreviews && !Report.Check(ShopUIPreview::Save(Root.Get(), Name), TEXT("Render current page or explicitly skip unavailable renderer: ") + Name)) return false;
            }
            return bPass;
        }

        bool Click(UUserWidget* Owner, FName Name)
        {
            UButton* Button = Child<UButton>(Owner, Name);
            if (!Report.Check(Button && Button->OnClicked.IsBound(), FString::Printf(TEXT("Actual OnClicked bound: %s.%s"), *GetNameSafe(Owner), *Name.ToString()))) return false;
            const bool bEnabled = Button->bIsEnabledDelegate.IsBound() ? Button->bIsEnabledDelegate.Execute() : Button->GetIsEnabled();
            if (!Report.Check(bEnabled, FString::Printf(TEXT("Button is enabled before click: %s.%s"), *GetNameSafe(Owner), *Name.ToString()))) return false;
            if (!Report.Check(IsVisibleThroughParents(Button) && IsVisibleThroughParents(Owner),
                FString::Printf(TEXT("Button and ancestor visibility allow clicking: %s.%s"), *GetNameSafe(Owner), *Name.ToString()))) return false;
            UUserWidget* PageOwner = Owner;
            if (Owner && Owner->GetName().StartsWith(TEXT("Card_")))
            {
                if (Snapshot().Phase == EGamePhase::Sell) PageOwner = Page(TEXT("ShelfPage"));
                else if (Snapshot().Phase == EGamePhase::Restock) PageOwner = Page(TEXT("MerchantPage"));
                else if (Snapshot().Phase == EGamePhase::Inside) PageOwner = Page(TEXT("InsidePage"));
            }
            if (!Report.Check(Pages && Pages->GetActiveWidget() == PageOwner,
                TEXT("Button belongs to the active page (not a hidden switcher page)"))) return false;
            Button->OnClicked.Broadcast();
            if (Owner && Owner->GetName().StartsWith(TEXT("Card_")) && Snapshot().Phase == EGamePhase::Restock)
                return VerifyMerchantPurchaseOpen();
            return true;
        }

        static bool IsVisibleThroughParents(const UWidget* Widget)
        {
            for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
            {
                const ESlateVisibility Visibility = Current->VisibilityDelegate.IsBound()
                    ? Current->VisibilityDelegate.Execute() : Current->GetVisibility();
                if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden ||
                    Visibility == ESlateVisibility::HitTestInvisible) return false;
            }
            return Widget != nullptr;
        }

        static ESlateVisibility EffectiveVisibility(const UWidget* Widget)
        {
            return Widget ? (Widget->VisibilityDelegate.IsBound() ? Widget->VisibilityDelegate.Execute() : Widget->GetVisibility())
                : ESlateVisibility::Collapsed;
        }

        static bool HasFixedCanvasRect(const UWidget* Widget, FVector2D Position, FVector2D Size)
        {
            const UCanvasPanelSlot* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
            return Slot && !Slot->GetAutoSize() && Slot->GetPosition().Equals(Position, 0.01f) && Slot->GetSize().Equals(Size, 0.01f) &&
                Slot->GetAlignment().Equals(FVector2D::ZeroVector, 0.01f) &&
                Slot->GetAnchors().Minimum.Equals(FVector2D::ZeroVector, 0.01f) && Slot->GetAnchors().Maximum.Equals(FVector2D::ZeroVector, 0.01f);
        }

        bool VerifyQuietShopScene()
        {
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            const TCHAR* Removed[] = { TEXT("Heading"), TEXT("PageTitle"), TEXT("Subtitle"), TEXT("CounterStatus"),
                TEXT("Counter"), TEXT("CounterTop"), TEXT("CounterLabel"), TEXT("ShopkeeperName"), TEXT("BtnCustomer"),
                TEXT("ShopkeeperPlaceholder"), TEXT("ShopkeeperBody"), TEXT("ShopkeeperHead"), TEXT("ShopkeeperHair"),
                TEXT("ShopkeeperEyeLeft"), TEXT("ShopkeeperEyeRight"), TEXT("ShopkeeperApron"),
                TEXT("CustomerCoat"), TEXT("CustomerHead"), TEXT("CustomerHair"), TEXT("CustomerEyes"), TEXT("CustomerBook"),
                TEXT("CustomerLegs"), TEXT("CustomerShoes") };
            for (const TCHAR* Name : Removed)
                if (!Report.Check(Child<UWidget>(Shop, FName(Name)) == nullptr,
                    TEXT("Scene omits obsolete captions, overlay counter, visible customer button and geometric people: ") + FString(Name))) return false;
            UScaleBox* Frame = Child<UScaleBox>(Shop, TEXT("FirstFloorFrame"));
            USizeBox* FloorSize = Child<USizeBox>(Shop, TEXT("FirstFloorSize"));
            UCanvasPanel* FloorScene = Child<UCanvasPanel>(Shop, TEXT("FirstFloorScene"));
            UImage* Background = Child<UImage>(Shop, TEXT("Background"));
            if (!Report.Check(Frame && FloorSize && FloorScene && Background &&
                HasFixedCanvasRect(Frame, FVector2D(0, 130), FVector2D(1920, 900)) && Frame->Stretch == EStretch::ScaleToFit &&
                FloorSize->GetParent() == Frame && FMath::IsNearlyEqual(FloorSize->GetWidthOverride(), 1440.f) &&
                FMath::IsNearlyEqual(FloorSize->GetHeightOverride(), 675.f) && FloorScene->GetParent() == FloorSize &&
                (FloorScene->GetClipping() == EWidgetClipping::ClipToBounds || FloorScene->GetClipping() == EWidgetClipping::ClipToBoundsAlways) &&
                Background->GetParent() == FloorScene && HasFixedCanvasRect(Background, FVector2D(-240, -405), FVector2D(1920, 1080)),
                TEXT("Shop renders only the cropped first floor: clipped1440x675 scene, full-size art offset(-240,-405), uniform1920x900 frame"))) return false;
            UPanelWidget* Keeper = Child<UPanelWidget>(Shop, TEXT("ShopkeeperGroup"));
            UPanelWidget* Customer = Child<UPanelWidget>(Shop, TEXT("CustomerGroup"));
            const ESlateVisibility Visibility = EffectiveVisibility(Keeper);
            if (!Report.Check(Keeper && Customer && Keeper->GetParent() == FloorScene && Customer->GetParent() == FloorScene &&
                HasFixedCanvasRect(Keeper, FVector2D(905, 200), FVector2D(160, 225)) &&
                HasFixedCanvasRect(Customer, FVector2D(661, 283), FVector2D(320, 300)) &&
                !Keeper->VisibilityDelegate.IsBound() && Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden &&
                Keeper->GetChildrenCount() > 0,
                TEXT("Protagonist and small customer use first-floor-relative positions inside the same cropped and scaled scene"))) return false;
            UScaleBox* KeeperFrame = Child<UScaleBox>(Shop, TEXT("ShopkeeperFrame"));
            UImage* Portrait = Child<UImage>(Shop, TEXT("ShopkeeperPortrait"));
            UWidgetSwitcher* Portraits = Child<UWidgetSwitcher>(Shop, TEXT("CustomerPortraits"));
            UButton* Hit = Child<UButton>(Shop, TEXT("BtnPortraitHit"));
            return Report.Check(KeeperFrame && Portrait && KeeperFrame->GetParent() == Keeper && Portrait->GetParent() == KeeperFrame &&
                HasFixedCanvasRect(KeeperFrame, FVector2D::ZeroVector, FVector2D(160, 225)) && KeeperFrame->Stretch == EStretch::ScaleToFit &&
                EffectiveVisibility(KeeperFrame) == ESlateVisibility::HitTestInvisible && EffectiveVisibility(Portrait) == ESlateVisibility::HitTestInvisible &&
                Portraits && Portraits->GetParent() == Customer && Portraits->GetChildrenCount() == 8 &&
                HasFixedCanvasRect(Portraits, FVector2D::ZeroVector, FVector2D(320, 300)) && Hit && Hit->GetParent() == Customer &&
                HasFixedCanvasRect(Hit, FVector2D::ZeroVector, FVector2D(320, 300)),
                TEXT("Real protagonist and eight customer portraits preserve aspect ratio; a separate full-size hit target selects the customer"));
        }

        bool VerifyCustomerPortraitSelection(int32 ExpectedStableSlot = INDEX_NONE)
        {
            TSet<int32> Reserved;
            for (const auto& Queued : Run->GetCustomers_Implementation())
            {
                if (!Report.Check(Queued.PortraitSlot >= 0 && Queued.PortraitSlot < 8 && !Reserved.Contains(Queued.PortraitSlot),
                    TEXT("Today's served and waiting visitors reserve distinct actual portrait slots"))) return false;
                Reserved.Add(Queued.PortraitSlot);
            }
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            UWidgetSwitcher* Portraits = Child<UWidgetSwitcher>(Shop, TEXT("CustomerPortraits"));
            const int32 Slot = UShopPresentationLibrary::GetCustomerPortraitSlot(Root.Get());
            if (!Report.Check(Portraits && Portraits->GetChildrenCount() == 8, TEXT("Customer portrait switcher contains eight authored alternatives"))) return false;
            if (!Run->IsCurrentCustomerPresent())
            {
                // UE5.1's Slate switcher clamps -1 to zero. The parent visibility, rather than
                // its dormant child index, must prevent a waiting visitor being displayed.
                return Report.Check(Slot == INDEX_NONE && EffectiveVisibility(Child<UWidget>(Shop, TEXT("CustomerGroup"))) == ESlateVisibility::Collapsed,
                    TEXT("Waiting has no portrait identity and hides the whole customer group"));
            }
            const int32 Index = UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get());
            const auto Customers = Run->GetCustomers_Implementation();
            if (!Report.Check(Customers.IsValidIndex(Index), TEXT("Portrait identity resolves the real present queue entry"))) return false;
            const ECustomerKind Kind = Customers[Index].Kind;
            const bool bKindMatches = (Kind == ECustomerKind::Normal && Slot >= 0 && Slot <= 3) ||
                (Kind == ECustomerKind::Hurry && Slot == 4) || (Kind == ECustomerKind::Secret && Slot == 5) ||
                (Kind == ECustomerKind::Polluted && Slot >= 6 && Slot <= 7);
            return Report.Check(bKindMatches && Customers[Index].PortraitSlot == Slot && Portraits->GetActiveWidgetIndex() == Slot &&
                (ExpectedStableSlot == INDEX_NONE || Slot == ExpectedStableSlot),
                FString::Printf(TEXT("Actual portrait agrees with kind/helper and remains stable when required: day=%d customer=%d kind=%d slot=%d active=%d"),
                    Snapshot().Day, Index, static_cast<int32>(Kind), Slot, Portraits->GetActiveWidgetIndex()));
        }

        bool VerifyUnselectedPanels()
        {
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            UWidget* Interaction = Child<UWidget>(Shop, TEXT("InteractionPanel"));
            UWidget* Need = Child<UWidget>(Shop, TEXT("NeedPanel"));
            return Report.Check(Interaction && Need && EffectiveVisibility(Interaction) == ESlateVisibility::Collapsed &&
                EffectiveVisibility(Need) == ESlateVisibility::Collapsed,
                TEXT("Before the character is clicked, both interaction and demand panels remain Collapsed"));
        }

        bool WaitForArrival()
        {
            if (!Report.Check(Snapshot().Phase == EGamePhase::Day, TEXT("Await visitor only in the actual daytime phase"))) return false;
            const auto Before = Run->GetCustomers_Implementation();
            int32 Index = INDEX_NONE;
            for (int32 Entry = 0; Entry < Before.Num(); ++Entry) if (!Before[Entry].bServed) { Index = Entry; break; }
            if (!Report.Check(Index != INDEX_NONE, TEXT("Arrival has an existing unserved queue entry"))) return false;
            if (!Run->IsCurrentCustomerPresent())
            {
                if (!Report.Check(!Snapshot().bCustomerPresent && Snapshot().CustomerArrivalRemaining > 0.f &&
                    Snapshot().CustomerArrivalRemaining <= SavedRules.CustomerArrivalMax &&
                    UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get()) == INDEX_NONE,
                    TEXT("Waiting queue does not expose an active customer before arrival"))) return false;
                if (!VerifyUnselectedPanels() || !VerifyCustomerPortraitSelection()) return false;
                UUserWidget* Shop = Page(TEXT("ShopPage"));
                if (!Report.Check(!IsVisibleThroughParents(Child<UWidget>(Shop, TEXT("CustomerGroup"))) &&
                    !IsVisibleThroughParents(Child<UWidget>(Shop, TEXT("InteractionPanel"))),
                    TEXT("Waiting hides portrait and interaction controls through real visibility bindings"))) return false;
                if (bCaptureSceneStates && !bCapturedSceneStates && Snapshot().Day == 1 && Index == 0)
                    if (!VerifyQuietShopScene() || !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("scene_waiting")),
                        TEXT("Render the first standard waiting scene"))) return false;
                // Tick only the arrival interval, never the visitor's subsequent patience interval.
                for (int32 Guard = 0; Guard < 100 && !Run->IsCurrentCustomerPresent(); ++Guard)
                    Run->Tick(FMath::Min(0.1f, Snapshot().CustomerArrivalRemaining));
            }
            const auto After = Run->GetCustomers_Implementation();
            if (!Report.Check(Run->IsCurrentCustomerPresent() && Snapshot().bCustomerPresent &&
                UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get()) == Index && After.Num() == Before.Num() &&
                After.IsValidIndex(Index) && !After[Index].bServed && FMath::IsNearlyEqual(After[Index].Patience, Before[Index].Patience),
                TEXT("Arrival reveals exactly the pending visitor without consuming patience or duplicating the queue")) ||
                !VerifyUnselectedPanels() || !VerifyCustomerPortraitSelection()) return false;
            if (bCaptureSceneStates && !bCapturedSceneStates && Snapshot().Day == 1 && Index == 0)
                if (!VerifyQuietShopScene() || !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("scene_arrived")),
                    TEXT("Render arrived character before any selection"))) return false;
            return true;
        }

        bool SelectVisitor()
        {
            return WaitForArrival() && Click(Page(TEXT("ShopPage")), TEXT("BtnPortraitHit"));
        }

        bool TextEquals(UUserWidget* Owner, FName Name, const FText& Expected)
        {
            UTextBlock* Text = Child<UTextBlock>(Owner, Name);
            if (!Report.Check(Text && Text->TextDelegate.IsBound(), FString::Printf(TEXT("Actual text delegate bound: %s.%s"), *GetNameSafe(Owner), *Name.ToString()))) return false;
            const FText Actual = Text->TextDelegate.Execute();
            return Report.Check(Actual.ToString() == Expected.ToString(), FString::Printf(TEXT("Bound text %s.%s = %s"),
                *GetNameSafe(Owner), *Name.ToString(), *Actual.ToString()));
        }

        bool VerifyHudValues()
        {
            struct FHudField { const TCHAR* WidgetName; EShopStatField Field; };
            const FHudField Fields[] = {
                { TEXT("HUD_Day"), EShopStatField::Day }, { TEXT("HUD_Money"), EShopStatField::Money },
                { TEXT("HUD_Psychic"), EShopStatField::Psychic }, { TEXT("HUD_Pollution"), EShopStatField::Pollution },
                { TEXT("HUD_Queue"), EShopStatField::Queue }, { TEXT("HUD_Stock"), EShopStatField::Stock },
                { TEXT("HUD_Enlighten"), EShopStatField::Enlighten }
            };
            for (const FHudField& Field : Fields)
                if (!TextEquals(Root.Get(), FName(Field.WidgetName), UShopPresentationLibrary::GetHudStatText(Root.Get(), Field.Field))) return false;
            return true;
        }

        bool VerifyProvidedTexture(UObject* Resource, const TCHAR* AssetName, const TCHAR* SourceFilename, int32 Width, int32 Height)
        {
            UTexture2D* Texture = Cast<UTexture2D>(Resource);
            const FString Expected = ObjectPath(AssetRoot + TEXT("Art/Provided/") + AssetName);
            if (!Report.Check(Texture && Texture->GetPathName() == Expected, TEXT("Live art resource is the intended texture: ") + Expected)) return false;
            if (!Report.Check(Texture->Source.IsValid() && Texture->Source.GetSizeX() == Width && Texture->Source.GetSizeY() == Height,
                FString::Printf(TEXT("Provided art retains source dimensions: %s %d x %d"), AssetName, Width, Height))) return false;
            const FString Source = Texture->AssetImportData ? Texture->AssetImportData->GetFirstFilename() : FString();
            return Report.Check(!Source.IsEmpty() && FPaths::GetCleanFilename(Source) == SourceFilename && !Source.Contains(TEXT("示意")),
                FString::Printf(TEXT("Provided art imports the blank/live-data source instead of a static reference: %s <- %s"), AssetName, *Source));
        }

        bool VerifyCharacterArtwork()
        {
            const TCHAR* Names[] = { TEXT("T_HeroMale"), TEXT("T_HeroFemale"), TEXT("T_Normal01"), TEXT("T_Normal02"), TEXT("T_Normal03"),
                TEXT("T_Normal04"), TEXT("T_Hurry"), TEXT("T_Secret"), TEXT("T_Polluted01"), TEXT("T_Polluted02"), TEXT("T_Merchant") };
            const TCHAR* Files[] = { TEXT("主角立绘/主角男装形象.PNG"), TEXT("主角立绘/主角女装形象.PNG"), TEXT("普通客人/IMG_6128.PNG"),
                TEXT("普通客人/IMG_6130.PNG"), TEXT("普通客人/IMG_6132.PNG"), TEXT("普通客人/IMG_6133.PNG"), TEXT("赶时间的人.PNG"),
                TEXT("戴帽兜的神秘人.PNG"), TEXT("被污染的客人/IMG_6125.PNG"), TEXT("被污染的客人/IMG_6127.PNG"), TEXT("merchant_cutout.png") };
            const FString SourceDirectory = FPaths::ProjectDir() / TEXT("SourceArt/UI/Characters/Original/角色与猫头鹰立绘");
            TMap<FName, FIntRect> Bounds;
            IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
            {
                const int32 Size = Index == 10 ? 1254 : 512;
                UTexture2D* Texture = Load<UTexture2D>(AssetRoot + TEXT("Art/Characters/") + Names[Index]);
                if (!Report.Check(Texture && Texture->Source.IsValid() && Texture->Source.GetNumBlocks() == 1 && !Texture->VirtualTextureStreaming &&
                    Texture->Source.GetSizeX() == Size && Texture->Source.GetSizeY() == Size &&
                    Texture->Source.GetFormat() == TSF_BGRA8 && Texture->LODGroup == TEXTUREGROUP_UI && Texture->CompressionSettings == TC_EditorIcon &&
                    !Texture->CompressionNoAlpha && Texture->MipGenSettings == TMGS_NoMipmaps && Texture->NeverStream && Texture->SRGB,
                    TEXT("Imported character is one non-virtual BGRA8 image with expected size and UI alpha settings, never a UDIM set: ") + FString(Names[Index]))) return false;
                const FString ExpectedSource = FPaths::ConvertRelativePathToFull(Index == 10
                    ? FPaths::ProjectDir() / TEXT("SourceArt/UI/Characters/Merchant/merchant_cutout.png") : SourceDirectory / Files[Index]);
                const FString ImportedSource = Texture->AssetImportData ? Texture->AssetImportData->GetFirstFilename() : FString();
                if (!Report.Check(!ImportedSource.IsEmpty() && FPaths::IsSamePath(ImportedSource, ExpectedSource),
                    TEXT("Character provenance matches its project source PNG: ") + FString(Names[Index]))) return false;
                TArray64<uint8> Pixels;
                if (!Report.Check(Texture->Source.GetMipData(Pixels, 0) && Pixels.Num() == Size * Size * 4,
                    TEXT("Read actual saved character source pixels: ") + FString(Names[Index]))) return false;
                TArray<uint8> Png;
                const TSharedPtr<IImageWrapper> Decoder = Images.CreateImageWrapper(EImageFormat::PNG);
                TArray64<uint8> Original;
                if (!Report.Check(FFileHelper::LoadFileToArray(Png, *ExpectedSource) && Decoder.IsValid() &&
                    Decoder->SetCompressed(Png.GetData(), Png.Num()) && Decoder->GetWidth() == Size && Decoder->GetHeight() == Size &&
                    Decoder->GetBitDepth() == 8 && (Decoder->GetFormat() == ERGBFormat::RGBA || Decoder->GetFormat() == ERGBFormat::BGRA) &&
                    Decoder->GetRaw(Original) && Original.Num() == Pixels.Num(),
                    TEXT("Decode the project character PNG independently of its texture asset: ") + FString(Names[Index]))) return false;
                const bool bOriginalRgba = Decoder->GetFormat() == ERGBFormat::RGBA;
                bool bExactPixels = true;
                int32 Left = Size, Top = Size, Right = 0, Bottom = 0;
                uint8 MinAlpha = 255, MaxAlpha = 0;
                for (int32 Y = 0; Y < Size; ++Y)
                    for (int32 X = 0; X < Size; ++X)
                    {
                        const int32 Offset = (Y * Size + X) * 4;
                        const uint8 Alpha = Pixels[Offset + 3];
                        // UE's default PNG importer fills RGB under zero alpha to prevent
                        // sampling fringes. All alpha and every visible RGB pixel must match.
                        bExactPixels &= Alpha == Original[Offset + 3] && (Alpha == 0 ||
                            (Pixels[Offset] == Original[Offset + (bOriginalRgba ? 2 : 0)] &&
                             Pixels[Offset + 1] == Original[Offset + 1] && Pixels[Offset + 2] == Original[Offset + (bOriginalRgba ? 0 : 2)]));
                        MinAlpha = FMath::Min(MinAlpha, Alpha); MaxAlpha = FMath::Max(MaxAlpha, Alpha);
                        if (Alpha > 0)
                        {
                            Left = FMath::Min(Left, X); Top = FMath::Min(Top, Y);
                            Right = FMath::Max(Right, X + 1); Bottom = FMath::Max(Bottom, Y + 1);
                        }
                    }
                if (!Report.Check(bExactPixels && MinAlpha == 0 && MaxAlpha > 0 && Right > Left && Bottom > Top,
                    TEXT("All alpha and visible RGB pixels equal the project source PNG; UV bounds exclude only fully transparent margins: ") + FString(Names[Index]))) return false;
                Bounds.Add(FName(Names[Index]), FIntRect(Left, Top, Right, Bottom));
            }
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            UWidgetSwitcher* Portraits = Child<UWidgetSwitcher>(Shop, TEXT("CustomerPortraits"));
            if (!VerifyQuietShopScene()) return false;
            for (int32 Index = 0; Index < 10; ++Index)
            {
                UUserWidget* Owner = Index == 9 ? Page(TEXT("MerchantPage")) : Shop;
                const FName FrameName = Index == 0 ? FName(TEXT("ShopkeeperFrame")) : Index == 9 ? FName(TEXT("MerchantFrame")) :
                    FName(*FString::Printf(TEXT("CustomerPortraitFrame%d"), Index - 1));
                const FName ImageName = Index == 0 ? FName(TEXT("ShopkeeperPortrait")) : Index == 9 ? FName(TEXT("MerchantPortrait")) :
                    FName(*FString::Printf(TEXT("CustomerPortrait%d"), Index - 1));
                const FName TextureName = Index == 0 ? FName(TEXT("T_HeroMale")) : Index == 9 ? FName(TEXT("T_Merchant")) : FName(Names[Index + 1]);
                const float SourceSize = Index == 9 ? 1254.f : 512.f;
                UScaleBox* Frame = Child<UScaleBox>(Owner, FrameName);
                UImage* Portrait = Child<UImage>(Owner, ImageName);
                const UScaleBoxSlot* ImageSlot = Portrait ? Cast<UScaleBoxSlot>(Portrait->Slot) : nullptr;
                if (!Report.Check(Frame && Portrait && Portrait->GetParent() == Frame && Frame->Stretch == EStretch::ScaleToFit &&
                    ImageSlot && ImageSlot->GetHorizontalAlignment() == HAlign_Center && ImageSlot->GetVerticalAlignment() == VAlign_Bottom &&
                    EffectiveVisibility(Frame) == ESlateVisibility::HitTestInvisible && EffectiveVisibility(Portrait) == ESlateVisibility::HitTestInvisible &&
                    (Index == 0 || Index == 9 || (Frame->GetParent() == Portraits && Portraits->GetChildAt(Index - 1) == Frame)),
                    TEXT("Real image lives in a non-blocking ScaleToFit frame in the authored fixed order: ") + ImageName.ToString())) return false;
                const FSlateBrush Brush = Portrait->BrushDelegate.IsBound() ? Portrait->BrushDelegate.Execute() : Portrait->Brush;
                const FIntRect* Rectangle = Bounds.Find(TextureName);
                const FBox2f UV = Brush.GetUVRegion();
                if (!Report.Check(Rectangle && Brush.GetResourceObject() &&
                    Brush.GetResourceObject()->GetPathName() == ObjectPath(AssetRoot + TEXT("Art/Characters/") + TextureName.ToString()) &&
                    UV.bIsValid && UV.Min.Equals(FVector2f(Rectangle->Min.X / SourceSize, Rectangle->Min.Y / SourceSize), 0.000001f) &&
                    UV.Max.Equals(FVector2f(Rectangle->Max.X / SourceSize, Rectangle->Max.Y / SourceSize), 0.000001f) &&
                    Brush.GetImageSize().Equals(FVector2D(Rectangle->Width(), Rectangle->Height()), 0.01f),
                    TEXT("Portrait brush selects exact alpha>0 bounds, exclusive maximum, no padding, and natural cropped dimensions: ") + ImageName.ToString())) return false;
            }
            return true;
        }

        bool VerifyProvidedArt()
        {
            struct FHudArt { const TCHAR* WidgetName; const TCHAR* AssetName; const TCHAR* SourceName; int32 Width; int32 Height; };
            const FHudArt Images[] = {
                { TEXT("HUD_MoneyArt"), TEXT("T_HUDMoney"), TEXT("金钱.png"), 241, 55 },
                { TEXT("HUD_StockArt"), TEXT("T_HUDStock"), TEXT("库存.png"), 207, 55 },
                { TEXT("HUD_PsychicArt"), TEXT("T_HUDPsychic"), TEXT("灵能2.png"), 146, 46 },
                { TEXT("HUD_PollutionArt"), TEXT("T_HUDPollutionIcon"), TEXT("污染icon.png"), 36, 37 },
                { TEXT("HUD_EnlightenArt"), TEXT("T_HUDEnlightenIcon"), TEXT("启蒙icon.png"), 45, 46 }
            };
            for (const FHudArt& Art : Images)
            {
                UImage* Widget = Child<UImage>(Root.Get(), FName(Art.WidgetName));
                if (!Report.Check(Widget != nullptr, TEXT("Runtime HUD image exists: ") + FString(Art.WidgetName))) return false;
                const FSlateBrush Brush = Widget->BrushDelegate.IsBound() ? Widget->BrushDelegate.Execute() : Widget->Brush;
                if (!VerifyProvidedTexture(Brush.GetResourceObject(), Art.AssetName, Art.SourceName, Art.Width, Art.Height)) return false;
            }
            const TCHAR* Owners[] = { TEXT("TutorialPage"), TEXT("ShopPage") };
            const TCHAR* Borders[] = { TEXT("DialoguePaper"), TEXT("NeedPanel") };
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(Owners); ++Index)
            {
                UBorder* Border = Child<UBorder>(Page(Owners[Index]), FName(Borders[Index]));
                if (!Report.Check(Border != nullptr, TEXT("Runtime dialogue border exists: ") + FString(Borders[Index]))) return false;
                const FSlateBrush Brush = Border->BackgroundDelegate.IsBound() ? Border->BackgroundDelegate.Execute() : Border->Background;
                if (!VerifyProvidedTexture(Brush.GetResourceObject(), TEXT("T_Dialogue"), TEXT("对话框.png"), 1853, 272)) return false;
            }
            TArray<UWidget*> Widgets;
            Root->WidgetTree->GetAllWidgets(Widgets);
            int32 HudTextCount = 0;
            for (UWidget* Widget : Widgets)
                if (Cast<UTextBlock>(Widget) && Widget->GetName().StartsWith(TEXT("HUD_"))) ++HudTextCount;
            return Report.Check(HudTextCount == 7, TEXT("Root contains exactly seven runtime HUD text fields")) && VerifyHudValues() && VerifyCharacterArtwork();
        }

        bool VerifyRejectToast()
        {
            UTextBlock* Toast = Child<UTextBlock>(Root.Get(), TEXT("Toast"));
            return Report.Check(Toast && Toast->GetText().ToString() == TEXT("已谢绝这位顾客。"),
                TEXT("Customer rejection callback displays its dedicated toast without a generic error"));
        }

        bool VerifyFixedCardFooter(UUserWidget* Card, bool bHasUnlist, float& CommonPrimaryY)
        {
            USizeBox* Size = Child<USizeBox>(Card, TEXT("CardSize"));
            UBorder* Paper = Child<UBorder>(Card, TEXT("CardPaper"));
            UCanvasPanel* Canvas = Child<UCanvasPanel>(Card, TEXT("CardCanvas"));
            UCanvasPanel* Actions = Child<UCanvasPanel>(Card, TEXT("ActionArea"));
            UButton* Primary = Child<UButton>(Card, TEXT("BtnPrimary"));
            UButton* Unlist = Child<UButton>(Card, TEXT("BtnUnlist"));
            UTextBlock* Feedback = Child<UTextBlock>(Card, TEXT("Feedback"));
            UScrollBox* Description = Child<UScrollBox>(Card, TEXT("DescriptionScroll"));
            const float CardHeight = bHasUnlist ? 630.f : 570.f;
            const float FooterHeight = bHasUnlist ? 152.f : 92.f;
            if (!Report.Check(Size && Paper && Canvas && Actions && Primary && Feedback && Description &&
                FMath::IsNearlyEqual(Size->GetWidthOverride(), 380.f) && FMath::IsNearlyEqual(Size->GetHeightOverride(), CardHeight) &&
                Paper->GetParent() == Size && Canvas->GetParent() == Paper && Actions->GetParent() == Canvas &&
                Primary->GetParent() == Actions && Feedback->GetParent() == Actions && Description->GetParent() == Canvas &&
                HasFixedCanvasRect(Actions, FVector2D(0, 446), FVector2D(344, FooterHeight)) &&
                HasFixedCanvasRect(Primary, FVector2D(0, 0), FVector2D(344, 48)) &&
                HasFixedCanvasRect(Description, FVector2D(0, 280), FVector2D(344, 96)) &&
                HasFixedCanvasRect(Feedback, FVector2D(0, bHasUnlist ? 112 : 54), FVector2D(344, bHasUnlist ? 40 : 38)),
                TEXT("Actual card has an independent scrolling description and fixed primary-action/footer rectangles: ") + GetNameSafe(Card))) return false;
            if (!Report.Check(bHasUnlist ? (Unlist && Unlist->GetParent() == Actions &&
                    HasFixedCanvasRect(Unlist, FVector2D(0, 56), FVector2D(344, 48))) : Unlist == nullptr,
                TEXT("Only secret-management cards contain the separated unlist button"))) return false;
            const FMargin Padding = Paper->GetPadding();
            const UCanvasPanelSlot* ActionSlot = CastChecked<UCanvasPanelSlot>(Actions->Slot);
            const UCanvasPanelSlot* PrimarySlot = CastChecked<UCanvasPanelSlot>(Primary->Slot);
            const float FooterY = static_cast<float>(ActionSlot->GetPosition().Y);
            const float ActualFooterHeight = static_cast<float>(ActionSlot->GetSize().Y);
            const float PrimaryY = Padding.Top + FooterY + static_cast<float>(PrimarySlot->GetPosition().Y);
            if (CommonPrimaryY < 0.f) CommonPrimaryY = PrimaryY;
            return Report.Check(FMath::IsNearlyEqual(PrimaryY, CommonPrimaryY) &&
                FMath::IsNearlyEqual(Padding.Top + FooterY + ActualFooterHeight + Padding.Bottom, CardHeight),
                FString::Printf(TEXT("All sale/buy/secret cards align primary action at localY=%.1f and keep footer flush with card bottom: %s"), PrimaryY, *GetNameSafe(Card)));
        }

        bool VerifyCards()
        {
            float CommonPrimaryY = -1.f;
            const TCHAR* PageNames[] = { TEXT("ShelfPage"), TEXT("MerchantPage"), TEXT("InsidePage") };
            for (int32 PageIndex = 0; PageIndex < 3; ++PageIndex)
            {
                UUserWidget* Owner = Page(PageNames[PageIndex]);
                if (!Report.Check(Owner && Owner->WidgetTree, TEXT("Nested page exists: ") + FString(PageNames[PageIndex]))) return false;
                int32 Count = 0;
                for (FName Id : Run->GetBookIds())
                {
                    FBookData Data; int32 Stock = 0;
                    if (!Report.Check(Run->GetBookInfo_Implementation(Id, Data, Stock), TEXT("Catalog resolves runtime card: ") + Id.ToString())) return false;
                    if (PageIndex == 1 && Data.Layer != EBookLayer::Table) continue;
                    if (PageIndex == 2 && Data.Layer != EBookLayer::Inside) continue;
                    UUserWidget* Card = Child<UUserWidget>(Owner, FName(*(TEXT("Card_") + Id.ToString())));
                    if (!Report.Check(Card && Card->WidgetTree, TEXT("Runtime nested book card exists: ") + Id.ToString())) return false;
                    FNameProperty* Property = FindFProperty<FNameProperty>(Card->GetClass(), TEXT("BookId"));
                    if (!Report.Check(Property && Property->GetPropertyValue_InContainer(Card) == Id, TEXT("Serialized nested BookId survived instantiation: ") + Id.ToString())) return false;
                    FObjectPropertyBase* CoverProperty = FindFProperty<FObjectPropertyBase>(Card->GetClass(), TEXT("CoverTexture"));
                    UTexture2D* Cover = CoverProperty ? Cast<UTexture2D>(CoverProperty->GetObjectPropertyValue_InContainer(Card)) : nullptr;
                    const FString CoverPath = ObjectPath(AssetRoot + TEXT("Art/BookCovers/T_") + Id.ToString());
                    if (!Report.Check(Cover && Cover->GetPathName() == CoverPath, TEXT("Serialized card CoverTexture matches its book: ") + Id.ToString())) return false;
                    if (!Report.Check(Cover->Source.IsValid() && Cover->Source.GetSizeX() == 2480 && Cover->Source.GetSizeY() == 3508,
                        TEXT("Book cover retains original 2480 x 3508 source: ") + Id.ToString())) return false;
                    UImage* CoverImage = Child<UImage>(Card, TEXT("BookCover"));
                    if (!Report.Check(CoverImage && CoverImage->BrushDelegate.IsBound(), TEXT("Actual BookCover brush delegate is bound: ") + Id.ToString())) return false;
                    const FSlateBrush CoverBrush = CoverImage->BrushDelegate.Execute();
                    if (!Report.Check(CoverBrush.GetResourceObject() == Cover, TEXT("Actual brush binding uses this card's editable CoverTexture: ") + Id.ToString())) return false;
                    if (!TextEquals(Card, TEXT("BookTitle"), Data.DisplayName)) return false;
                    UTextBlock* StockText = Child<UTextBlock>(Card, TEXT("BookStock"));
                    UButton* Primary = Child<UButton>(Card, TEXT("BtnPrimary"));
                    if (!Report.Check(StockText && StockText->TextDelegate.IsBound() && !StockText->TextDelegate.Execute().IsEmpty(), TEXT("Runtime stock text binding: ") + Id.ToString())) return false;
                    if (!Report.Check(Primary && Primary->bIsEnabledDelegate.IsBound(), TEXT("Runtime action eligibility binding: ") + Id.ToString())) return false;
                    if (!VerifyFixedCardFooter(Card, PageIndex == 2, CommonPrimaryY)) return false;
                    ++Count;
                }
                if (!Report.Check(Count == (PageIndex == 0 ? 16 : PageIndex == 1 ? 9 : 7), TEXT("Complete card count: ") + FString(PageNames[PageIndex]))) return false;
            }
            return VerifyProvidedArt();
        }

        bool VerifyMerchantScene(bool bNewVisit = true)
        {
            UUserWidget* Merchant = Page(TEXT("MerchantPage"));
            UCanvasPanel* Panel = Child<UCanvasPanel>(Merchant, TEXT("PurchasePanel"));
            UPanelWidget* Person = Child<UPanelWidget>(Merchant, TEXT("MerchantGroup"));
            UButton* Hit = Child<UButton>(Merchant, TEXT("BtnMerchantHit"));
            UButton* Done = Child<UButton>(Merchant, TEXT("BtnDone"));
            UTextBlock* Feedback = Child<UTextBlock>(Merchant, TEXT("Feedback"));
            UScaleBox* PortraitFrame = Child<UScaleBox>(Merchant, TEXT("MerchantFrame"));
            UImage* Portrait = Child<UImage>(Merchant, TEXT("MerchantPortrait"));
            if (!Report.Check(Snapshot().Phase == EGamePhase::Restock && Panel && Person && Hit && Done && Feedback &&
                EffectiveVisibility(Panel) == ESlateVisibility::Collapsed && IsVisibleThroughParents(Person) && IsVisibleThroughParents(Hit) &&
                IsVisibleThroughParents(Done) && Person->GetParent() != Panel && Done->GetParent() != Panel &&
                HasFixedCanvasRect(Person, FVector2D(960, 456), FVector2D(320, 515)),
                TEXT("Merchant entry/closed view shows its scene, character and finish-night button while the purchase panel is Collapsed"))) return false;
            if (!Report.Check(PortraitFrame && Portrait && PortraitFrame->GetParent() == Person && Portrait->GetParent() == PortraitFrame &&
                PortraitFrame->Stretch == EStretch::ScaleToFit && HasFixedCanvasRect(PortraitFrame, FVector2D::ZeroVector, FVector2D(320, 515)) &&
                EffectiveVisibility(PortraitFrame) == ESlateVisibility::HitTestInvisible && EffectiveVisibility(Portrait) == ESlateVisibility::HitTestInvisible &&
                Hit->GetParent() == Person && HasFixedCanvasRect(Hit, FVector2D::ZeroVector, FVector2D(320, 515)) &&
                Portrait->Brush.GetResourceObject() && Portrait->Brush.GetResourceObject()->GetPathName() == ObjectPath(AssetRoot + TEXT("Art/Characters/T_Merchant")),
                TEXT("Merchant uses the original older character in a non-blocking proportional frame with a full-size separate hit target"))) return false;
            const TCHAR* Removed[] = { TEXT("MerchantPlaceholder"), TEXT("MerchantBody"), TEXT("MerchantHead"), TEXT("MerchantHair"),
                TEXT("MerchantHat"), TEXT("MerchantApron"), TEXT("MerchantEyes") };
            for (const TCHAR* Name : Removed)
                if (!Report.Check(Child<UWidget>(Merchant, FName(Name)) == nullptr, TEXT("Merchant has no obsolete geometric placeholder: ") + FString(Name))) return false;
            if (bNewVisit && !Report.Check(Feedback->GetText().IsEmpty(), TEXT("Entering a new merchant visit clears prior page feedback"))) return false;
            UUserWidget* FirstCard = Child<UUserWidget>(Merchant, TEXT("Card_book_novel"));
            return Report.Check(FirstCard && !IsVisibleThroughParents(FirstCard), TEXT("Collapsed purchase panel prevents hidden card interaction"));
        }

        bool VerifyMerchantPurchaseOpen()
        {
            UUserWidget* Merchant = Page(TEXT("MerchantPage"));
            UCanvasPanel* Panel = Child<UCanvasPanel>(Merchant, TEXT("PurchasePanel"));
            UButton* Close = Child<UButton>(Merchant, TEXT("BtnClosePurchase"));
            UButton* Done = Child<UButton>(Merchant, TEXT("BtnDone"));
            return Report.Check(Snapshot().Phase == EGamePhase::Restock && Panel && Close && Done &&
                EffectiveVisibility(Panel) == ESlateVisibility::SelfHitTestInvisible && IsVisibleThroughParents(Close) && IsVisibleThroughParents(Done),
                TEXT("Purchase panel is open and remains open through ordinary purchase callbacks; close and finish-night buttons are accessible"));
        }

        bool OpenMerchantPurchase(bool bCloseAndReopen = false)
        {
            if (!VerifyMerchantScene()) return false;
            const bool bCaptureVisit = bCaptureSceneStates && !bCapturedMerchantStates;
            UUserWidget* Merchant = Page(TEXT("MerchantPage"));
            const FRunSnapshot Before = Snapshot();
            if (bCaptureVisit && !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("merchant_scene")), TEXT("Render merchant scene before selecting the character"))) return false;
            if (!Click(Merchant, TEXT("BtnMerchantHit")) || !VerifyMerchantPurchaseOpen()) return false;
            if (bCaptureVisit && !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("merchant_purchase")), TEXT("Render purchasing opened through the actual merchant character"))) return false;
            if (bCloseAndReopen)
            {
                if (!Click(Merchant, TEXT("BtnClosePurchase")) || !VerifyMerchantScene(false)) return false;
                if (bCaptureVisit && !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("merchant_closed")), TEXT("Render return from purchasing to the merchant scene"))) return false;
                if (!Click(Merchant, TEXT("BtnMerchantHit")) || !VerifyMerchantPurchaseOpen()) return false;
            }
            if (bCaptureVisit) bCapturedMerchantStates = true;
            return Report.Check(Snapshot().Day == Before.Day && Snapshot().Money == Before.Money && Snapshot().TotalStock == Before.TotalStock &&
                Snapshot().TotalSold == Before.TotalSold && Snapshot().Phase == Before.Phase,
                TEXT("Opening/closing merchant UI does not buy anything, settle the night or mutate business state"));
        }

        bool Tutorial(EGamePhase BeforePhase = EGamePhase::Boot, EGamePhase BusinessPhase = EGamePhase::Day)
        {
            const int32 BeforeDay = Snapshot().Day;
            const int32 BeforeCount = Run->GetCustomers_Implementation().Num();
            if (!At(BeforePhase, 0, TEXT("Initial or returned menu")) || !Click(Page(TEXT("MenuPage")), TEXT("BtnStart"))) return false;
            if (!At(BeforePhase, 1, TEXT("Menu starts tutorial before business"))) return false;
            UUserWidget* Guide = Page(TEXT("TutorialPage"));
            UImage* Portrait = Child<UImage>(Guide, TEXT("OwlPortrait"));
            UTexture2D* Texture = Portrait ? Cast<UTexture2D>(Portrait->Brush.GetResourceObject()) : nullptr;
            if (!Report.Check(Texture && Texture->GetPathName() == TEXT("/Game/ProgramA/UI/Art/T_OwlGuide.T_OwlGuide"),
                TEXT("Actual tutorial OwlPortrait uses the imported T_OwlGuide Texture2D"))) return false;
            if (!Report.Check(Texture->LODGroup == TEXTUREGROUP_UI && Texture->CompressionSettings == TC_EditorIcon && !Texture->CompressionNoAlpha &&
                Texture->MipGenSettings == TMGS_NoMipmaps && Texture->NeverStream && Texture->SRGB,
                TEXT("Owl portrait has UI RGBA texture settings and preserves alpha"))) return false;
            const int32 SourceWidth = Texture->Source.GetSizeX(), SourceHeight = Texture->Source.GetSizeY();
            if (!Report.Check(Texture->Source.IsValid() && SourceWidth == 1024 && SourceHeight == 1536 && Texture->Source.GetFormat() == TSF_BGRA8,
                FString::Printf(TEXT("Owl texture contains a valid BGRA8 source (%d x %d)"), SourceWidth, SourceHeight))) return false;
            TArray64<uint8> Mip;
            if (!Report.Check(Texture->Source.GetMipData(Mip, 0) && Mip.Num() == static_cast<int64>(SourceWidth) * SourceHeight * 4,
                TEXT("Owl alpha is read from the actual saved texture source"))) return false;
            uint8 MinAlpha = 255, MaxAlpha = 0;
            for (int64 Offset = 3; Offset < Mip.Num(); Offset += 4)
            {
                MinAlpha = FMath::Min(MinAlpha, Mip[Offset]);
                MaxAlpha = FMath::Max(MaxAlpha, Mip[Offset]);
            }
            if (!Report.Check(MinAlpha == 0 && MaxAlpha >= 240, FString::Printf(TEXT("Owl source contains fully transparent and visible pixels (alpha %d..%d)"), MinAlpha, MaxAlpha))) return false;
            FIntProperty* Index = Guide ? FindFProperty<FIntProperty>(Guide->GetClass(), TEXT("DialogueIndex")) : nullptr;
            if (!Report.Check(Index != nullptr, TEXT("Tutorial instance contains DialogueIndex"))) return false;
            for (int32 Line = 0; Line < 10; ++Line)
            {
                if (!Report.Check(Index->GetPropertyValue_InContainer(Guide) == Line, TEXT("Tutorial advances one line per actual click")) ||
                    !TextEquals(Guide, TEXT("DialogueText"), UShopPresentationLibrary::GetTutorialText(Line))) return false;
                Run->Tick(5.f);
                if (!Report.Check(Snapshot().Phase == BeforePhase && Snapshot().Day == BeforeDay && Run->GetCustomers_Implementation().Num() == BeforeCount, TEXT("Tutorial does not advance business or generate customers"))) return false;
                if (!Click(Guide, Line % 2 == 0 ? TEXT("BtnNext") : TEXT("BtnDialogueHit"))) return false;
            }
            return At(BusinessPhase, BusinessPhase == EGamePhase::Calm ? 8 : 2, TEXT("Tenth click starts real business")) &&
                Report.Check(Snapshot().Day == 1 && Run->GetCustomers_Implementation().Num() == 3, TEXT("Day one contains exactly three customers"));
        }

        bool ServeDay()
        {
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            if (!WaitForArrival()) return false;
            const int32 BeforeIndex = UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get());
            const int32 PortraitSlot = UShopPresentationLibrary::GetCustomerPortraitSlot(Root.Get());
            const auto BeforeQueue = Run->GetCustomers_Implementation();
            if (!Report.Check(BeforeIndex == 0 && BeforeQueue.IsValidIndex(BeforeIndex) && !BeforeQueue[BeforeIndex].bServed,
                TEXT("A fresh day starts at the first unserved customer"))) return false;
            if (!Click(Shop, TEXT("BtnPortraitHit"))) return false;
            UWidget* Bubble = Child<UWidget>(Shop, TEXT("NeedPanel"));
            if (!Report.Check(IsVisibleThroughParents(Bubble), TEXT("Portrait hit area exposes the actual bound demand panel"))) return false;
            if (bCaptureSceneStates && !bCapturedSceneStates)
            {
                if (!Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("scene_selected")), TEXT("Render the first character selection and expanded operation panel"))) return false;
                bCapturedSceneStates = true;
            }
            if (!Click(Shop, TEXT("BtnPortraitHit"))) return false;
            const auto AfterClicks = Run->GetCustomers_Implementation();
            if (!Report.Check(UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get()) == BeforeIndex && AfterClicks.Num() == BeforeQueue.Num() &&
                AfterClicks.IsValidIndex(BeforeIndex) && !AfterClicks[BeforeIndex].bServed && IsVisibleThroughParents(Bubble),
                TEXT("Repeated character selection reveals the same visitor without serving or skipping them"))) return false;
            if (!Click(Shop, TEXT("BtnObserve"))) return false;
            const auto Queue = Run->GetCustomers_Implementation();
            if (!Report.Check(Queue.IsValidIndex(0) && Queue[0].bObserved, TEXT("Observe button reaches the service"))) return false;
            if (!VerifyCustomerPortraitSelection(PortraitSlot)) return false;
            const FRunSnapshot BeforeRefresh = Snapshot();
            const float PatienceBeforeRefresh = Queue[0].Patience;
            IShopView::Execute_RefreshShop(Root.Get(), BeforeRefresh);
            IShopView::Execute_RefreshShop(Root.Get(), BeforeRefresh);
            if (!VerifyCustomerPortraitSelection(PortraitSlot) || !Report.Check(Snapshot().Money == BeforeRefresh.Money &&
                Snapshot().TotalSold == BeforeRefresh.TotalSold && Snapshot().TotalStock == BeforeRefresh.TotalStock &&
                FMath::IsNearlyEqual(Run->GetCustomers_Implementation()[0].Patience, PatienceBeforeRefresh),
                TEXT("Repeated same-state refresh retains the actual portrait without changing inventory, money, service or patience"))) return false;
            if (!TextEquals(Shop, TEXT("NeedText"), Run->BuildCustomerNeedText(0))) return false;
            if (bCapturePreviews && !Report.Check(ShopUIPreview::Save(Root.Get(), PreviewPrefix + TEXT("_demand_day") + FString::FromInt(Snapshot().Day)),
                TEXT("Render the expanded demand dialogue after actual customer and observation clicks"))) return false;
            FName BookId; FBookData Book;
            for (FName Id : Run->GetBookIds())
            {
                FBookData Data; int32 Stock = 0;
                if (Run->GetBookInfo_Implementation(Id, Data, Stock) && Stock > 0 &&
                    Data.BookType == Queue[0].NeedType && Data.Layer == Queue[0].NeedLayer) { BookId = Id; Book = Data; break; }
            }
            if (!Report.Check(!BookId.IsNone(), TEXT("First ordinary visitor has a real stocked product"))) return false;
            FBookRuntime BeforeBook; Run->GetBookRuntime(BookId, BeforeBook);
            const int32 BeforeMoney = Snapshot().Money;
            if (!Click(Shop, TEXT("BtnShelf")) || !At(EGamePhase::Sell, 3, TEXT("Shelf button opens sale page"))) return false;
            if (!VerifyCustomerPortraitSelection(PortraitSlot) || !Click(Page(TEXT("ShelfPage")), TEXT("BtnDone")) ||
                !At(EGamePhase::Day, 2, TEXT("Actual cancel-selection button returns to the same visitor")) ||
                !VerifyCustomerPortraitSelection(PortraitSlot)) return false;
            FBookRuntime AfterCancel; Run->GetBookRuntime(BookId, AfterCancel);
            if (!Report.Check(UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get()) == BeforeIndex &&
                !Run->GetCustomers_Implementation()[BeforeIndex].bServed && Snapshot().Money == BeforeMoney &&
                AfterCancel.Stock == BeforeBook.Stock && FMath::IsNearlyEqual(Run->GetCustomers_Implementation()[BeforeIndex].Patience, PatienceBeforeRefresh),
                TEXT("Opening/cancelling selection keeps this customer and their portrait without spending inventory or patience"))) return false;
            if (!Click(Shop, TEXT("BtnShelf")) || !At(EGamePhase::Sell, 3, TEXT("Reopen shelf for the same customer after cancellation")) ||
                !VerifyCustomerPortraitSelection(PortraitSlot)) return false;
            UUserWidget* Card = Child<UUserWidget>(Page(TEXT("ShelfPage")), FName(*(TEXT("Card_") + BookId.ToString())));
            if (!Click(Card, TEXT("BtnPrimary")) || !At(EGamePhase::Day, 2, TEXT("Book-card button completes sale"))) return false;
            FBookRuntime AfterBook; Run->GetBookRuntime(BookId, AfterBook);
            if (!Report.Check(Snapshot().Money == BeforeMoney + Book.Price && AfterBook.Stock == BeforeBook.Stock - 1 && Snapshot().TotalSold == 1,
                TEXT("Actual sale pays price and removes exactly one copy"))) return false;
            if (!Report.Check(Run->GetCustomers_Implementation()[0].bServed, TEXT("Actual sale resolves customer zero"))) return false;
            if (!SelectVisitor() || !Click(Shop, TEXT("BtnReject")) || !At(EGamePhase::Day, 2, TEXT("Reject second customer"))) return false;
            if (!VerifyRejectToast()) return false;
            if (!SelectVisitor() || !Click(Shop, TEXT("BtnReject")) || !At(EGamePhase::DayEnd, 4, TEXT("Third resolution automatically opens day settlement"))) return false;
            if (!VerifyRejectToast()) return false;
            return Click(Page(TEXT("DayEndPage")), TEXT("BtnContinue")) && At(EGamePhase::DuskChoice, 5, TEXT("Settlement continues to night choice"));
        }

        bool Night(bool bInside)
        {
            if (!Click(Page(TEXT("NightChoicePage")), bInside ? TEXT("BtnInside") : TEXT("BtnMerchant"))) return false;
            if (!At(bInside ? EGamePhase::Inside : EGamePhase::Restock, bInside ? 7 : 6, TEXT("Selected night branch"))) return false;
            if (!bInside && !OpenMerchantPurchase(true)) return false;
            UUserWidget* Owner = Page(bInside ? TEXT("InsidePage") : TEXT("MerchantPage"));
            FName BookId; FBookData Data;
            for (FName Id : Run->GetBookIds())
            {
                int32 Stock = 0; FBookData Candidate;
                if (Run->GetBookInfo_Implementation(Id, Candidate, Stock) &&
                    Candidate.Layer == (bInside ? EBookLayer::Inside : EBookLayer::Table)) { BookId = Id; Data = Candidate; break; }
            }
            if (!Report.Check(!BookId.IsNone(), TEXT("Night branch product exists"))) return false;
            UUserWidget* Card = Child<UUserWidget>(Owner, FName(*(TEXT("Card_") + BookId.ToString())));
            FBookRuntime Before; Run->GetBookRuntime(BookId, Before);
            const FRunSnapshot BeforeState = Snapshot();
            if (!Click(Card, TEXT("BtnPrimary"))) return false;
            FBookRuntime After; Run->GetBookRuntime(BookId, After);
            if (bInside)
            {
                if (!Report.Check(After.Stock == Before.Stock && After.ListedCopies == Before.ListedCopies + 1 &&
                    After.StoredCopies == Before.StoredCopies - 1 && Snapshot().Money == BeforeState.Money && Snapshot().Pollution == BeforeState.Pollution,
                    TEXT("Real listing button conserves owned copies, money and pollution"))) return false;
                if (!Click(Card, TEXT("BtnUnlist"))) return false;
                Run->GetBookRuntime(BookId, After);
                if (!Report.Check(After.ListedCopies == Before.ListedCopies && After.StoredCopies == Before.StoredCopies, TEXT("Actual unlist button restores stored/listed split"))) return false;
                if (!Click(Card, TEXT("BtnPrimary"))) return false;
            }
            else if (!Report.Check(After.Stock == Before.Stock + 1 && Snapshot().Money == BeforeState.Money - Data.Cost,
                TEXT("Real merchant card buys one book and pays its authored cost"))) return false;
            if (!Click(Owner, TEXT("BtnDone")) || !At(EGamePhase::NightEnd, 9, TEXT("Night branch settles through its Done button"))) return false;
            if (!Click(Page(TEXT("NightEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::Day, 2, TEXT("Night settlement opens next day"))) return false;
            if (!Report.Check(Snapshot().Day == 2 && Run->GetCustomers_Implementation().Num() == 3, TEXT("Next day has one fresh three-person queue"))) return false;
            if (bInside)
            {
                Run->GetBookRuntime(BookId, After);
                if (!Report.Check(After.ListedCopies == Before.ListedCopies + 1, TEXT("Listing persists across night/day boundary"))) return false;
            }
            return Report.Check(!IsVisibleThroughParents(Child<UWidget>(Page(TEXT("ShopPage")), TEXT("InteractionPanel"))),
                TEXT("NewDay callback resets customer interaction UI while the next visitor is still arriving"));
        }

        bool PollutedPortraitsAndTrade()
        {
            if (!Click(Page(TEXT("DecreePage")), TEXT("BtnSkip")) || !At(EGamePhase::Day, 2, TEXT("Polluted fixture skips initial calm through real button"))) return false;
            TSet<int32> Seen;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                if (!WaitForArrival()) return false;
                const auto Queue = Run->GetCustomers_Implementation();
                const FCustomerRuntime Customer = Queue[Index];
                const int32 Slot = UShopPresentationLibrary::GetCustomerPortraitSlot(Root.Get());
                if (!Report.Check(Customer.Kind == ECustomerKind::Polluted && Customer.bPolluted && Slot >= 6 && Slot <= 7 && !Seen.Contains(Slot),
                    TEXT("Both actual polluted visitors use different supplied polluted portraits"))) return false;
                Seen.Add(Slot);
                if (!Report.Check(ShopUIPreview::Save(Root.Get(), FString::Printf(TEXT("special_polluted_%d"), Index + 1)), TEXT("Render actually generated polluted visitor"))) return false;
                UUserWidget* Shop = Page(TEXT("ShopPage"));
                if (!Click(Shop, TEXT("BtnPortraitHit")) || !Click(Shop, TEXT("BtnObserve")) || !VerifyCustomerPortraitSelection(Slot) ||
                    !Report.Check(Run->GetCustomers_Implementation()[Index].bObserved, TEXT("Polluted observation records actual customer state"))) return false;
                if (Index == 0)
                {
                    if (!Click(Shop, TEXT("BtnReject")) || !Report.Check(Run->GetCustomers_Implementation()[Index].bServed, TEXT("Polluted visitor can be refused"))) return false;
                    continue;
                }
                FName BookId; FBookData Data; int32 Stock = 0;
                for (FName Id : Run->GetBookIds())
                {
                    if (Run->GetBookInfo_Implementation(Id, Data, Stock) && Data.Layer == EBookLayer::Table && Data.BookType == Customer.NeedType && Stock > 0)
                    { BookId = Id; break; }
                }
                if (!Report.Check(!BookId.IsNone(), TEXT("Polluted visitor requests an available ordinary book"))) return false;
                const FRunSnapshot Before = Snapshot();
                if (!Click(Shop, TEXT("BtnShelf"))) return false;
                UUserWidget* Card = Child<UUserWidget>(Page(TEXT("ShelfPage")), FName(*(TEXT("Card_") + BookId.ToString())));
                if (!Click(Card, TEXT("BtnPrimary")) || !Report.Check(Snapshot().Money == Before.Money + Data.Price &&
                    Snapshot().Pollution == Before.Pollution + SavedRules.PollutionOnPollutedCustomer &&
                    Run->GetCustomers_Implementation()[Index].Resolution == EShopActionResult::Sold,
                    TEXT("Polluted ordinary-book sale applies the actual configured price and pollution"))) return false;
            }
            return true;
        }

        bool SecretSaleAndDecree()
        {
            if (!WaitForArrival()) return false;
            const auto Queue = Run->GetCustomers_Implementation();
            if (!Report.Check(Snapshot().Day == 2 && Queue.Num() == 3 && Queue[0].Kind == ECustomerKind::Secret,
                TEXT("Short fixture reserves a real secret visitor for its listed stock"))) return false;
            if (!VerifyCustomerPortraitSelection(5) || !Report.Check(ShopUIPreview::Save(Root.Get(), TEXT("special_secret")),
                TEXT("Real generated secret visitor uses and renders the supplied hooded portrait"))) return false;
            FName BookId; FBookData Data; FBookRuntime Before;
            for (FName Id : Run->GetBookIds())
            {
                FBookRuntime Book;
                if (Run->GetBookRuntime(Id, Book) && Book.ListedCopies > 0)
                { BookId = Id; Before = Book; int32 Stock; Run->GetBookInfo_Implementation(Id, Data, Stock); break; }
            }
            if (!Report.Check(!BookId.IsNone(), TEXT("A genuinely listed copy is available to the secret visitor"))) return false;
            const FRunSnapshot BeforeState = Snapshot();
            UUserWidget* Shop = Page(TEXT("ShopPage"));
            if (!Click(Shop, TEXT("BtnPortraitHit")) || !Click(Shop, TEXT("BtnShelf")) || !At(EGamePhase::Sell, 3, TEXT("Secret visitor enters the real shelf page"))) return false;
            UUserWidget* Card = Child<UUserWidget>(Page(TEXT("ShelfPage")), FName(*(TEXT("Card_") + BookId.ToString())));
            if (!Click(Card, TEXT("BtnPrimary")) || !At(EGamePhase::Calm, 8, TEXT("Secret sale crosses threshold and opens the actual decree page"))) return false;
            FBookRuntime After; Run->GetBookRuntime(BookId, After);
            if (!Report.Check(After.Stock == Before.Stock - 1 && After.ListedCopies == Before.ListedCopies - 1 &&
                Snapshot().Money == BeforeState.Money + Data.Price && Snapshot().TotalSold == BeforeState.TotalSold + 1 &&
                Run->GetCustomers_Implementation()[0].bServed, TEXT("Threshold-crossing sale commits exactly once before the modal callback"))) return false;
            const auto Paused = Run->GetCustomers_Implementation();
            Run->Tick(5.f);
            if (!Report.Check(Snapshot().Phase == EGamePhase::Calm && Run->GetCustomers_Implementation()[1].Patience == Paused[1].Patience,
                TEXT("Actual calm page pauses the remaining customer's patience"))) return false;
            const FRunSnapshot CalmState = Snapshot();
            int32 Candidate = INDEX_NONE;
            for (int32 Index = 0; Index < CalmState.DecreeCandidates.Num(); ++Index)
                if (UShopPresentationLibrary::CanChooseDecree(Root.Get(), Index)) { Candidate = Index; break; }
            if (!Report.Check(Candidate != INDEX_NONE, TEXT("Real UI decree candidates include an affordable choice"))) return false;
            if (!TextEquals(Page(TEXT("DecreePage")), FName(*FString::Printf(TEXT("DecreeTitle%d"), Candidate)), UShopPresentationLibrary::GetDecreeTitle(Root.Get(), Candidate))) return false;
            if (!Click(Page(TEXT("DecreePage")), FName(*FString::Printf(TEXT("BtnDecree%d"), Candidate))) ||
                !At(EGamePhase::Day, 2, TEXT("Actual decree click resumes daytime trading"))) return false;
            return Report.Check(Snapshot().Pollution < CalmState.Pollution && Snapshot().TotalSold == CalmState.TotalSold &&
                Snapshot().Money <= CalmState.Money, TEXT("Decree applies its actual cost/effect without replaying the sale"));
        }

        bool FinishShortRun()
        {
            if (!Report.Check(Snapshot().MaxDays == 2, TEXT("Only the labelled fixture has a two-day limit"))) return false;
            int32 Remaining = 0;
            for (const auto& Customer : Run->GetCustomers_Implementation()) if (!Customer.bServed) ++Remaining;
            for (int32 Index = 0; Index < Remaining; ++Index)
                if (!SelectVisitor() || !Click(Page(TEXT("ShopPage")), TEXT("BtnReject"))) return false;
            if (!At(EGamePhase::DayEnd, 4, TEXT("Fixture final day settlement"))) return false;
            if (!Click(Page(TEXT("DayEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::DuskChoice, 5, TEXT("Fixture final night choice"))) return false;
            if (!Click(Page(TEXT("NightChoicePage")), TEXT("BtnMerchant")) || !At(EGamePhase::Restock, 6, TEXT("Fixture final night activity"))) return false;
            if (!VerifyMerchantScene()) return false;
            if (!Click(Page(TEXT("MerchantPage")), TEXT("BtnDone")) || !At(EGamePhase::NightEnd, 9, TEXT("Fixture final night settlement"))) return false;
            if (!Click(Page(TEXT("NightEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::End, 10, TEXT("Actual final continue opens the ending page"))) return false;
            if (!Report.Check(Snapshot().Ending != EShopEnding::None && Snapshot().Day == 2, TEXT("Ending is resolved without creating an extra day"))) return false;
            return TextEquals(Page(TEXT("EndingPage")), TEXT("EndingTitle"), UShopPresentationLibrary::GetEndingTitle(Root.Get())) &&
                TextEquals(Page(TEXT("EndingPage")), TEXT("EndingStory"), Snapshot().EndMessage);
        }

        bool VerifyEnding(EShopEnding Expected, const FString& Label)
        {
            if (!At(EGamePhase::End, 10, Label) ||
                !Report.Check(Snapshot().Ending == Expected, Label + TEXT(": exact business ending"))) return false;
            UTextBlock* Toast = Child<UTextBlock>(Root.Get(), TEXT("Toast"));
            if (!Report.Check(Toast && Toast->GetText().IsEmpty(), TEXT("Ending clears the root toast instead of retaining the last sale or rejection"))) return false;
            const FRunSnapshot Before = Snapshot();
            Run->Tick(60.f);
            if (!Report.Check(Snapshot().Day == Before.Day && Snapshot().Money == Before.Money &&
                Snapshot().TotalSold == Before.TotalSold && Snapshot().Ending == Expected,
                TEXT("Ending stops customer timers and cannot silently create the next day"))) return false;
            UDataTable* Table = Load<UDataTable>(TEXT("/Game/ProgramA/Release/Data/DT_Endings"));
            const FEndingData* Authored = nullptr;
            if (Table)
                for (FName Id : Table->GetRowNames())
                    if (const FEndingData* Row = Table->FindRow<FEndingData>(Id, TEXT("UIVerify")))
                        if (Row->Ending == Expected) { Authored = Row; break; }
            if (!Report.Check(Authored && Snapshot().EndMessage.EqualTo(Authored->Text),
                TEXT("Business ending text comes from the matching original DT_Endings row"))) return false;
            if (!TextEquals(Page(TEXT("EndingPage")), TEXT("EndingTitle"), Authored->Title) ||
                !TextEquals(Page(TEXT("EndingPage")), TEXT("EndingStory"), Authored->Text) ||
                !TextEquals(Page(TEXT("EndingPage")), TEXT("EndingCondition"), UShopPresentationLibrary::GetEndingConditionText(Root.Get()))) return false;
            Report.Note(FString::Printf(TEXT("ENDING VERIFIED scenario=%s ending=%d day=%d/%d money=%d enlightenment=%d pollution=%d"),
                *PreviewPrefix, static_cast<int32>(Expected), Snapshot().Day, Snapshot().MaxDays,
                Snapshot().Money, Snapshot().Enlighten, Snapshot().Pollution));
            return !bCapturePreviews || Report.Check(ShopUIPreview::Save(Root.Get(), PreviewPrefix + TEXT("_ending")), TEXT("Render verified ending"));
        }

        FName PreferredBook(EBookType Type, bool bProgressive, bool bRequireStock) const
        {
            FName Best;
            int32 BestScore = MIN_int32;
            for (FName Id : Run->GetBookIds())
            {
                FBookData Book; int32 Stock = 0;
                if (!Run->GetBookInfo_Implementation(Id, Book, Stock) || Book.Layer != EBookLayer::Table || Book.BookType != Type ||
                    (bRequireStock && Stock <= 0)) continue;
                const bool bGrantsEnlightenment = Book.SaleEnlightenChance > 0.f && Book.SaleEnlightenYield > 0;
                if (!bProgressive && bGrantsEnlightenment) continue;
                const int32 Score = (bGrantsEnlightenment ? 100000 : 0) + Book.Price - Book.Cost;
                if (Score > BestScore) { Best = Id; BestScore = Score; }
            }
            // The authored first-day stock has only two non-progressive history copies.
            // If all three visitors ask for history, sell an owned alternative once;
            // subsequent nights restock the non-progressive book and preserve the Cycle strategy.
            if (Best.IsNone() && !bProgressive && bRequireStock) return PreferredBook(Type, true, true);
            return Best;
        }

        bool SellCurrentOrdinary(bool bProgressive)
        {
            if (!SelectVisitor()) return false;
            const int32 Index = UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get());
            const auto Customers = Run->GetCustomers_Implementation();
            if (!Report.Check(Customers.IsValidIndex(Index) && Customers[Index].NeedLayer == EBookLayer::Table && !Customers[Index].bFake,
                TEXT("Unlisted ordinary-only strategy attracts a serviceable ordinary visitor"))) return false;
            const FName Id = PreferredBook(Customers[Index].NeedType, bProgressive, true);
            if (!Report.Check(!Id.IsNone(), TEXT("Real inventory supplies the current demand without rewriting stock"))) return false;
            FBookRuntime Before; Run->GetBookRuntime(Id, Before);
            const int32 Sales = Snapshot().TotalSold;
            if (!Click(Page(TEXT("ShopPage")), TEXT("BtnShelf")) || !At(EGamePhase::Sell, 3, TEXT("Full-term shelf opened by click")) ||
                !Click(Child<UUserWidget>(Page(TEXT("ShelfPage")), FName(*(TEXT("Card_") + Id.ToString()))), TEXT("BtnPrimary"))) return false;
            FBookRuntime After; Run->GetBookRuntime(Id, After);
            return Report.Check(After.Stock == Before.Stock - 1 && Snapshot().TotalSold == Sales + 1 &&
                Run->GetCustomers_Implementation()[Index].bServed, TEXT("Full-term actual card commits one sale and one inventory decrement"));
        }

        bool RefillOrdinary(bool bProgressive)
        {
            if (!OpenMerchantPurchase()) return false;
            for (EBookType Type : { EBookType::Novel, EBookType::Poem, EBookType::History })
            {
                const FName Id = PreferredBook(Type, bProgressive, false);
                if (!Report.Check(!Id.IsNone(), TEXT("Restock strategy resolves an authored ordinary book"))) return false;
                FBookData Data; int32 Stock = 0;
                if (!Run->GetBookInfo_Implementation(Id, Data, Stock)) return false;
                // Three copies cover even three visitors requesting the same category tomorrow.
                for (int32 Count = Stock; Count < 3; ++Count)
                {
                    if (!Report.Check(Snapshot().Money >= Data.Cost, TEXT("Full-term purchases are funded by actual trading income"))) return false;
                    const int32 BeforeMoney = Snapshot().Money;
                    if (!Click(Child<UUserWidget>(Page(TEXT("MerchantPage")), FName(*(TEXT("Card_") + Id.ToString()))), TEXT("BtnPrimary"))) return false;
                    int32 Updated = 0; FBookData Ignored;
                    if (!Run->GetBookInfo_Implementation(Id, Ignored, Updated) ||
                        !Report.Check(Updated == Count + 1 && Snapshot().Money == BeforeMoney - Data.Cost,
                            TEXT("Full-term merchant click pays and restocks exactly one copy"))) return false;
                }
            }
            return true;
        }

        bool FullThirtyFiveDays(bool bProgressive)
        {
            bCapturePreviews = false;
            if (!Report.Check(Snapshot().MaxDays == 35 && FixtureTables.IsEmpty(),
                TEXT("Full-term scenario uses exact saved data: 35 days and zero fixture tables"))) return false;
            for (int32 Day = 1; Day <= 35; ++Day)
            {
                if (!At(EGamePhase::Day, 2, TEXT("Full-term daily opening")) ||
                    !Report.Check(Snapshot().Day == Day && Snapshot().Ending == EShopEnding::None,
                        TEXT("Exactly one next day, no early final-threshold ending"))) return false;
                for (int32 Customer = 0; Customer < 3; ++Customer) if (!SellCurrentOrdinary(bProgressive)) return false;
                if (!At(EGamePhase::DayEnd, 4, TEXT("Full-term third visitor settles the day")) ||
                    !Click(Page(TEXT("DayEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::DuskChoice, 5, TEXT("Full-term dusk choice")) ||
                    !Click(Page(TEXT("NightChoicePage")), TEXT("BtnMerchant")) || !At(EGamePhase::Restock, 6, TEXT("Full-term merchant"))) return false;
                if (Day < 35 && !RefillOrdinary(bProgressive)) return false;
                if (Day == 35 && !VerifyMerchantScene()) return false;
                if (!Click(Page(TEXT("MerchantPage")), TEXT("BtnDone")) || !At(EGamePhase::NightEnd, 9, TEXT("Full-term completed night settlement"))) return false;
                Report.Note(FString::Printf(TEXT("FULL35 day=%d seed-strategy=%s money=%d enlightenment=%d pollution=%d sold=%d"),
                    Day, bProgressive ? TEXT("progressive") : TEXT("ordinary"), Snapshot().Money,
                    Snapshot().Enlighten, Snapshot().Pollution, Snapshot().TotalSold));
                if (!Click(Page(TEXT("NightEndPage")), TEXT("BtnContinue"))) return false;
                if (Day < 35 && !Report.Check(Snapshot().Phase == EGamePhase::Day && Snapshot().Day == Day + 1 && Snapshot().Ending == EShopEnding::None,
                    TEXT("Before day35 the night-continue button opens the following day, not an ending"))) return false;
            }
            return At(EGamePhase::End, 10, TEXT("Full-term day35 final continue opens ending")) &&
                Report.Check(Snapshot().Day == 35 && Snapshot().TotalSold == 105,
                    TEXT("All 105 real visitors resolved over exactly35 days; no day36 was created"));
        }

        bool BankruptcyByRejection()
        {
            bCapturePreviews = false;
            for (int32 Guard = 0; Guard < 35 && Snapshot().Phase != EGamePhase::End; ++Guard)
            {
                for (int32 Visitor = 0; Visitor < 3; ++Visitor)
                    if (!SelectVisitor() || !Click(Page(TEXT("ShopPage")), TEXT("BtnReject"))) return false;
                if (Snapshot().Phase == EGamePhase::End) break;
                if (!At(EGamePhase::DayEnd, 4, TEXT("No-sale day settles authored rent")) ||
                    !Click(Page(TEXT("DayEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::DuskChoice, 5, TEXT("No-sale dusk")) ||
                    !Click(Page(TEXT("NightChoicePage")), TEXT("BtnMerchant")) ||
                    !VerifyMerchantScene() ||
                    !Click(Page(TEXT("MerchantPage")), TEXT("BtnDone"))) return false;
                if (Snapshot().Phase == EGamePhase::End) break;
                if (!Click(Page(TEXT("NightEndPage")), TEXT("BtnContinue"))) return false;
            }
            if (!Report.Check(FixtureTables.IsEmpty() && Snapshot().NegativeDays >= 3 && Snapshot().Day < 35,
                TEXT("Authored rent and three negative days trigger early closure with no altered data"))) return false;
            bCapturePreviews = true;
            return VerifyEnding(EShopEnding::Closed, TEXT("Real-data bankruptcy ending"));
        }

        bool PollutionBoundarySale()
        {
            if (!SelectVisitor()) return false;
            FName Id;
            for (FName Candidate : Run->GetBookIds())
            {
                FBookRuntime Book;
                if (Run->GetBookRuntime(Candidate, Book) && Book.ListedCopies > 0) { Id = Candidate; break; }
            }
            if (!Report.Check(!Id.IsNone(), TEXT("Pollution boundary uses an actually listed secret copy")) ||
                !Click(Page(TEXT("ShopPage")), TEXT("BtnShelf")) ||
                !Click(Child<UUserWidget>(Page(TEXT("ShelfPage")), FName(*(TEXT("Card_") + Id.ToString()))), TEXT("BtnPrimary"))) return false;
            return Report.Check(Snapshot().Pollution >= SavedRules.PollutionLimit && Snapshot().MaxDays == 35 && Snapshot().Day == 2,
                TEXT("Actual secret-sale transaction reaches the original ending-table pollution limit")) &&
                VerifyEnding(EShopEnding::PollutionReleased, TEXT("Transient high-pollution sale boundary"));
        }

        bool VerifyRestartState()
        {
            if (!At(EGamePhase::Day, 2, TEXT("Restart returns to the actual shop page")) ||
                !Report.Check(Snapshot().Day == 1 && Snapshot().TotalSold == 0 && Run->GetCustomers_Implementation().Num() == 3,
                    TEXT("Restart resets day, sales and queue"))) return false;
            for (FName Id : Run->GetBookIds())
            {
                FBookRuntime Book; Run->GetBookRuntime(Id, Book);
                if (!Report.Check(Book.ListedCopies == 0, TEXT("Restart clears listed copies: ") + Id.ToString())) return false;
            }
            return true;
        }
    };
}

UShopUIVerifyCommandlet::UShopUIVerifyCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UShopUIVerifyCommandlet::Main(const FString& Params)
{
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("UMGEditor"));
    FReport Report;
    Report.Note(TEXT("Saved WBP verification: real generated-class instances, delegate bindings and OnClicked Blueprint handlers."));
    Report.Note(TEXT("Headless verification does not assert pixel layout, mouse hit testing, AddToViewport or packaged rendering."));
    bool bAssetsReady = true;
    bAssetsReady &= VerifyAuthoredEndings(Report);
    TArray<TStrongObjectPtr<UBlueprint>> Assets;
    for (const TCHAR* Relative : WidgetAssets)
    {
        const FString Path = AssetRoot + Relative;
        UWidgetBlueprint* Blueprint = Load<UWidgetBlueprint>(Path);
        Assets.Emplace(Blueprint);
        bAssetsReady &= Compiled(Report, Blueprint, Path);
        if (Blueprint) bAssetsReady &= Report.Check(Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget &&
            Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UUserWidget::StaticClass()), TEXT("Editable Designer tree and real widget class: ") + Path);
    }
    UBlueprint* Flow = Load<UBlueprint>(AssetRoot + TEXT("Framework/BPI_UIFlow"));
    UBlueprint* PC = Load<UBlueprint>(AssetRoot + TEXT("Framework/BP_UIPlayerController"));
    UBlueprint* GM = Load<UBlueprint>(AssetRoot + TEXT("Framework/BP_UIGameMode"));
    Assets.Emplace(Flow); Assets.Emplace(PC); Assets.Emplace(GM);
    bAssetsReady &= Compiled(Report, Flow, TEXT("BPI_UIFlow"));
    bAssetsReady &= Compiled(Report, PC, TEXT("BP_UIPlayerController"));
    bAssetsReady &= Compiled(Report, GM, TEXT("BP_UIGameMode"));
    UWorld* Entry = Load<UWorld>(AssetRoot + TEXT("Maps/L_BookstoreUI"));
    bAssetsReady &= Report.Check(Entry && Entry->GetOutermost()->HasAnyPackageFlags(PKG_ContainsMap), TEXT("Saved entry .umap loads as a map package"));
    UWidgetBlueprint* Root = Load<UWidgetBlueprint>(AssetRoot + TEXT("WBP_UIRoot"));
    if (bAssetsReady)
    {
        {
            FSession Polluted(Report);
            if (!Polluted.Start(PC->GeneratedClass, Root->GeneratedClass, true, false, 731, false, true) ||
                !Polluted.Tutorial(EGamePhase::Boot, EGamePhase::Calm) || !Polluted.PollutedPortraitsAndTrade())
                Report.Note(TEXT("Polluted portrait fixture stopped at its first failure; no runtime state or saved data was rewritten."));
        }
        const AShopPlayerController* PCCDO = Cast<AShopPlayerController>(PC->GeneratedClass->GetDefaultObject());
        const AShopGameMode* GMCDO = Cast<AShopGameMode>(GM->GeneratedClass->GetDefaultObject());
        bAssetsReady &= Report.Check(PCCDO && Root && PCCDO->RootWidgetClass.Get() == Root->GeneratedClass.Get(), TEXT("PlayerController defaults select generated root WBP"));
        bAssetsReady &= Report.Check(GMCDO && !GMCDO->bStartNewRunOnFirstEntry && GMCDO->PlayerControllerClass.Get() == PC->GeneratedClass.Get(), TEXT("GameMode preserves menu/tutorial before business"));
        bAssetsReady &= Report.Check(Entry->GetWorldSettings()->DefaultGameMode.Get() == GM->GeneratedClass.Get(), TEXT("Entry map uses the generated GameMode"));
    }
    if (bAssetsReady)
    {
        for (const bool bInside : { false, true })
        {
            Report.Note(bInside ? TEXT("BRANCH inside listing") : TEXT("BRANCH merchant restock"));
            FSession Session(Report);
            if (!Session.Start(PC->GeneratedClass, Root->GeneratedClass, bInside) || !Session.VerifyCards() ||
                !Session.Tutorial() || !Session.ServeDay() || !Session.Night(bInside))
                Report.Note(TEXT("Branch stopped at its first failure; no substitute native requests were issued."));
        }
        Report.Note(TEXT("SCENARIO transient short fixture: actual secret sale/decree buttons, ending, direct restart, return-to-menu and tutorial restart."));
        FSession Short(Report);
        if (!Short.Start(PC->GeneratedClass, Root->GeneratedClass, true, true) || !Short.Tutorial() || !Short.ServeDay() ||
            !Short.Night(true) || !Short.SecretSaleAndDecree() || !Short.FinishShortRun() ||
            !Short.Click(Short.Page(TEXT("EndingPage")), TEXT("BtnRestart")) || !Short.VerifyRestartState() ||
            !Short.ServeDay() || !Short.Night(false) || !Short.FinishShortRun() ||
            !Short.Click(Short.Page(TEXT("EndingPage")), TEXT("BtnMenu")) || !Short.Tutorial(EGamePhase::End) || !Short.VerifyRestartState())
            Report.Note(TEXT("Short scenario stopped at its first failure; no saved tables or runtime state were rewritten."));

        Report.Note(TEXT("FULL35 CYCLE: exact saved tables, seed731; sell non-enlightenment ordinary books and purchase three-copy category buffers through actual buttons."));
        {
            FSession Cycle(Report);
            Cycle.bCapturePreviews = false;
            if (Cycle.Start(PC->GeneratedClass, Root->GeneratedClass, false) && Cycle.Tutorial() && Cycle.FullThirtyFiveDays(false))
            {
                Cycle.PreviewPrefix = TEXT("full35_cycle"); Cycle.bCapturePreviews = true;
                Cycle.VerifyEnding(EShopEnding::Cycle, TEXT("Real-data 35-day cycle ending"));
            }
        }
        Report.Note(TEXT("FULL35 RETURNED: exact saved tables, prefer the three authored 50%-chance +2 books; bounded seed search731..746 is recorded, never alter money/stock/enlightenment/state."));
        bool bReturnedReached = false;
        for (int32 Seed = 731; Seed <= 746 && !bReturnedReached; ++Seed)
        {
            const int32 FailuresBefore = Report.Failures;
            FSession Returned(Report);
            Returned.bCapturePreviews = false;
            if (!Returned.Start(PC->GeneratedClass, Root->GeneratedClass, false, false, Seed) || !Returned.Tutorial() || !Returned.FullThirtyFiveDays(true)) break;
            Report.Note(FString::Printf(TEXT("RETURNED SEED ATTEMPT seed=%d ending=%d enlightenment=%d money=%d"), Seed,
                static_cast<int32>(Returned.Snapshot().Ending), Returned.Snapshot().Enlighten, Returned.Snapshot().Money));
            if (Returned.Snapshot().Ending == EShopEnding::Returned)
            {
                Returned.PreviewPrefix = TEXT("full35_returned"); Returned.bCapturePreviews = true;
                bReturnedReached = Returned.VerifyEnding(EShopEnding::Returned, TEXT("Real-data 35-day returned ending"));
            }
            else if (!Report.Check(Returned.Snapshot().Ending == EShopEnding::Cycle,
                TEXT("An unlucky progressive run may legitimately fall back to Cycle; other endings indicate a strategy failure"))) break;
            if (Report.Failures != FailuresBefore) break;
        }
        Report.Check(bReturnedReached, TEXT("Returned is reachable through an entire real35-day button-driven run with recorded seed"));
        Report.Note(TEXT("CLOSED: exact saved data; reject each arrived visitor, pay authored rent, and wait for the actual negative-day condition."));
        {
            FSession Closed(Report);
            Closed.bCapturePreviews = false;
            if (Closed.Start(PC->GeneratedClass, Root->GeneratedClass, false) && Closed.Tutorial())
            {
                Closed.PreviewPrefix = TEXT("real_closed");
                Closed.BankruptcyByRejection();
            }
        }
        Report.Note(TEXT("POLLUTION RELEASED: labelled transient pollution-per-sale boundary; all actions use real WBP buttons and the exact saved four-row ending table."));
        {
            FSession Pollution(Report);
            Pollution.bCapturePreviews = false;
            if (Pollution.Start(PC->GeneratedClass, Root->GeneratedClass, true, false, 731, true) && Pollution.Tutorial() &&
                Pollution.ServeDay() && Pollution.Night(true))
            {
                Pollution.bCapturePreviews = true;
                Pollution.PollutionBoundarySale();
            }
        }
    }
    Report.Note(FString::Printf(TEXT("RESULT checks=%d failures=%d"), Report.Checks, Report.Failures));
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("UIBuild");
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveStringToFile(Report.Lines, *(Directory / TEXT("Verification.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        Report.Check(false, TEXT("Save verification report"));
    return Report.Failures == 0 ? 0 : 1;
}
