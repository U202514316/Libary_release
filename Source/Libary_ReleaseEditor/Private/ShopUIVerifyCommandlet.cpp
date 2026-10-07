#include "ShopUIVerifyCommandlet.h"
#include "ShopUIPreview.h"

#include "ShopGameMode.h"
#include "ShopPlayerController.h"
#include "ShopGameGuideWidget.h"
#include "ShopAudioComponent.h"
#include "ShopAudioPalette.h"
#include "ShopAudioLibrary.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
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
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogShopUIVerify, Log, All);

namespace
{
    const FString AssetRoot = TEXT("/Game/ProgramA/UI/");
    const TCHAR* WidgetAssets[] = {
        TEXT("Components/WBP_SaleBookCard"), TEXT("Components/WBP_MerchantBookCard"), TEXT("Components/WBP_SecretBookCard"),
        TEXT("WBP_MainMenu"), TEXT("WBP_OwlTutorial"), TEXT("WBP_SurfaceShop"), TEXT("WBP_Bookshelf"), TEXT("WBP_DaySettlement"),
        TEXT("WBP_NightChoice"), TEXT("WBP_Merchant"), TEXT("WBP_InsideShop"), TEXT("WBP_Decree"),
        TEXT("WBP_NightSettlement"), TEXT("WBP_Ending"), TEXT("WBP_UIRoot"),
        TEXT("Components/WBP_BlackMarketBookCard"), TEXT("WBP_HistoryFragment"), TEXT("WBP_BlackMarket"), TEXT("WBP_DecreeIntroduction"), TEXT("WBP_GameGuide")
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
        UDataTable* Table=Load<UDataTable>(TEXT("/Game/ProgramA/Release/Data/DT_Endings"));
        FString Source;
        TArray<TSharedPtr<FJsonValue>> Rows;
        if(!Report.Check(Table&&Table->GetRowStruct()==FEndingData::StaticStruct()&&Table->GetRowMap().Num()==5,
            TEXT("Saved ending table contains exactly the five manuscript endings")) ||
            !Report.Check(FFileHelper::LoadFileToString(Source,*(FPaths::ProjectDir()/TEXT("SourceData/Endings/ending_rows.json")))&&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Rows)&&Rows.Num()==5,TEXT("Read extracted manuscript rows")))return false;
        for(const auto& Value:Rows)
        {
            const auto Object=Value->AsObject();
            const FEndingData* Row=Table->FindRow<FEndingData>(FName(*Object->GetStringField(TEXT("Name"))),TEXT("FiveEndings"));
            if(!Report.Check(Row && Row->Title.ToString()==Object->GetStringField(TEXT("Title")) && Row->Text.ToString()==Object->GetStringField(TEXT("Text")) &&
                Row->Priority==Object->GetIntegerField(TEXT("Priority")) && Row->MinMoney==1500 &&
                Row->RedemptionCost==Object->GetIntegerField(TEXT("RedemptionCost")) && Row->bRequiresPlayerChoice==Object->GetBoolField(TEXT("bRequiresPlayerChoice")),
                TEXT("Saved title, complete story, priority and choice/cost match manuscript row: ")+Object->GetStringField(TEXT("Name"))))return false;
        }
        return true;
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
        bool bCaptureSmallPreviews = false;
        bool bCaptureSceneStates = false;
        bool bCapturedSceneStates = false;
        bool bCapturedMerchantStates = false;
        FRunRules SavedRules;
        FReport& Report;

        explicit FSession(FReport& InReport) : Instance(nullptr), Root(nullptr), Report(InReport) {}
        ~FSession()
        {
            if (Controller && Controller->ShopAudio)
            {
                Controller->ShopAudio->ShutdownAudio();
                Report.Check(!Controller->ShopAudio->IsAudioInitialized() && Controller->ShopAudio->CurrentMusic.IsNone(), TEXT("Audio observer and loop state cleaned up with session"));
            }
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
            int32 Seed = 731, bool bPollutionBoundary = false, bool bPollutedPortraitFixture = false, float HistoryChance = -1.f, int32 InitialPsychicFixture = -1)
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
            if (!Report.Check(SavedRules.PsychicMax == 0, TEXT("Saved rules no longer advertise a psychic cap"))) return false;
            if (!Report.Check(SavedRules.HistoryFragmentEnlightenGain == 10, TEXT("Saved new-fragment reward is ten enlightenment"))) return false;
            if (InitialPsychicFixture >= 0)
            {
                Rules = DuplicateObject<UDataTable>(Rules, GetTransientPackage()); FixtureTables.Emplace(Rules);
                Rules->FindRow<FRunRules>(Rules->GetRowNames()[0], TEXT("PsychicLayoutFixture"))->StartPsychic = InitialPsychicFixture;
                Report.Note(TEXT("TRANSIENT LARGE PSYCHIC FIXTURE ONLY: start psychic=1234567890; saved rules unchanged."));
            }
            if (HistoryChance >= 0.f)
            {
                Rules = DuplicateObject<UDataTable>(Rules, GetTransientPackage()); FixtureTables.Emplace(Rules);
                Rules->FindRow<FRunRules>(Rules->GetRowNames()[0],TEXT("HistoryFixture"))->HistoryFragmentChance = HistoryChance;
                Report.Note(FString::Printf(TEXT("TRANSIENT HISTORY PROBABILITY FIXTURE ONLY: chance=%.2f; all other saved rule values and seven authored texts retained."),HistoryChance));
            }
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
                Load<UDataTable>(AssetRoot + TEXT("Data/DT_Events_UI")), Load<UDataTable>(AssetRoot + TEXT("Data/DT_MarketItems_UI")),
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
            if (!Report.Check(Controller->AudioPalette && Controller->ShopAudio && Controller->ShopAudio->InitializeAudio(Controller->AudioPalette,false),
                TEXT("Saved player controller config initializes its audio observer without a physical output device"))) return false;
            if (!Report.Check(Run->RegisterView(Root.Get()), TEXT("Register actual root ShopView"))) return false;
            Pages = Child<UWidgetSwitcher>(Root.Get(), TEXT("Pages"));
            if (!Report.Check(Pages && Pages->GetChildrenCount() == 13, TEXT("Runtime root contains all thirteen real page instances"))) return false;
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
                if (!VerifyAudioScene()) return false;
                if (!VerifyHudValues()) return false;
                if ((Phase == EGamePhase::Day || Phase == EGamePhase::Sell) && !VerifyCustomerPortraitSelection()) return false;
                const FString Name = FString::Printf(TEXT("%s_%02d_page%d"), *PreviewPrefix, PreviewIndex++, PageIndex);
                // Rendering uses the real current page; the verifier never changes the switcher for a screenshot.
                if (bCapturePreviews && !Report.Check(ShopUIPreview::Save(Root.Get(), Name), TEXT("Render current page or explicitly skip unavailable renderer: ") + Name)) return false;
                if (bCaptureSmallPreviews && !Report.Check(ShopUIPreview::Save(Root.Get(), Name + TEXT("_720p"), FIntPoint(1280,720)),
                    TEXT("Render 720p and check actual text bounds: ") + Name)) return false;
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
            UUserWidget* Decree=Page(TEXT("DecreePage"));
            UWidgetSwitcher* DecreePages=Child<UWidgetSwitcher>(Decree,TEXT("DecreePages"));
            if(Owner==Child<UUserWidget>(Decree,TEXT("IntroductionPage")))
            {
                PageOwner=Decree;
                if(!Report.Check(DecreePages && DecreePages->GetActiveWidgetIndex()==1,TEXT("Introduction button belongs to the visible nested page")))return false;
            }
            else if(Owner==Decree && !Report.Check(DecreePages && DecreePages->GetActiveWidgetIndex()==0,TEXT("Selection button belongs to the visible nested page")))return false;
            if (Owner && Owner->GetName().StartsWith(TEXT("Card_")))
            {
                if (Snapshot().Phase == EGamePhase::Sell) PageOwner = Page(TEXT("ShelfPage"));
                else if (Snapshot().Phase == EGamePhase::Restock) PageOwner = Page(TEXT("MerchantPage"));
                else if (Snapshot().Phase == EGamePhase::Inside) PageOwner = Page(TEXT("InsidePage"));
                else if (Snapshot().Phase == EGamePhase::Market) PageOwner = Page(TEXT("BlackMarketPage"));
            }
            if (!Report.Check(Pages && (Pages->GetActiveWidget() == PageOwner || Owner == Root.Get()),
                TEXT("Button belongs to the active page (not a hidden switcher page)"))) return false;
            UShopAudioComponent* Audio=Controller->ShopAudio;
            if(!Report.Check(Button->OnHovered.IsBound(),TEXT("Interactive button has saved hover audio hook")))return false;
            const FRunSnapshot AudioBefore=Snapshot();
            const int32 SoldBefore=Audio->EventCount(TEXT("Sale_Success"));
            const int32 BuyBefore=Audio->EventCount(TEXT("Purchase_Normal"));
            const int32 BlackBuyBefore=Audio->EventCount(TEXT("Purchase_Secret"));
            const int32 ReadBefore=Audio->EventCount(TEXT("Read_Secret"));
            const int32 EnactBefore=Audio->EventCount(TEXT("Decree_Enact"));
            const int32 MusicBefore=Audio->MusicStartCount();
            Button->OnClicked.Broadcast();
            const FRunSnapshot AudioAfter=Snapshot();
            const bool Success=Run->GetLastResult().bSucceeded;
            if(!Report.Check(Audio->EventCount(TEXT("Sale_Success"))-SoldBefore==(AudioAfter.TotalSold>AudioBefore.TotalSold?1:0),TEXT("Cash sound occurs exactly once for a completed sale")))return false;
            if(Owner->GetName().StartsWith(TEXT("Card_")) && Name==TEXT("BtnPrimary"))
            {
                if(AudioBefore.Phase==EGamePhase::Restock && !Report.Check(Audio->EventCount(TEXT("Purchase_Normal"))-BuyBefore==(Success?1:0),TEXT("Normal purchase sound follows actual purchase result")))return false;
                if(AudioBefore.Phase==EGamePhase::Market && !Report.Check(Audio->EventCount(TEXT("Purchase_Secret"))-BlackBuyBefore==(Success?1:0),TEXT("Black-market purchase sound follows actual purchase result")))return false;
            }
            if(Name==TEXT("BtnRead") && !Report.Check(Audio->EventCount(TEXT("Read_Secret"))-ReadBefore==(Success?1:0),TEXT("Secret reading sound follows actual reading result")))return false;
            if(Owner==Page(TEXT("DecreePage")))
            {
                if(!Report.Check(Audio->EventCount(TEXT("Decree_Enact"))-EnactBefore==(Name==TEXT("BtnConfirm") && Success?1:0),TEXT("Decree selection/description cannot play enactment; successful confirm plays once")))return false;
            }
            if((Name==TEXT("BtnDecrees") || Name==TEXT("BtnIntroduction") || Name==TEXT("BtnSkip")) &&
                !Report.Check(Audio->MusicStartCount()==MusicBefore,TEXT("Modal navigation preserves current music playback position")))return false;
            if(!VerifyAudioScene())return false;
            if (Owner && Owner->GetName().StartsWith(TEXT("Card_")) && Snapshot().Phase == EGamePhase::Restock)
                return VerifyMerchantPurchaseOpen();
            return true;
        }

        bool VerifyAudioScene()
        {
            UShopAudioComponent* Audio=Controller->ShopAudio;
            const FRunSnapshot S=Snapshot();
            const bool Menu=Pages && Pages->GetActiveWidgetIndex()<2;
            const bool Modal=!Menu && (S.Phase==EGamePhase::Calm || S.Phase==EGamePhase::History);
            if(!Report.Check(Audio->bModalDucked==Modal,TEXT("Modal music ducking matches the displayed page")))return false;
            FName Expected=TEXT("Music_Title");
            if(!Menu && !Modal)
            {
                if(S.Phase==EGamePhase::Day || S.Phase==EGamePhase::Sell || S.Phase==EGamePhase::DayEnd)Expected=TEXT("Music_Shop");
                if(S.Phase==EGamePhase::Restock)Expected=TEXT("Music_Market");
                if(S.Phase==EGamePhase::Inside || S.Phase==EGamePhase::Market)Expected=TEXT("Music_Inside");
                if(S.Phase==EGamePhase::End)
                    Expected=(S.Ending==EShopEnding::Closed||S.Ending==EShopEnding::FailedRedemption)?TEXT("Music_End_Closed"):S.Ending==EShopEnding::PollutionReleased?TEXT("Music_End_Release"):
                        S.Ending==EShopEnding::Returned?TEXT("Music_End_Return"):TEXT("Music_End_Cycle");
            }
            if(!Modal && !Report.Check(Audio->CurrentMusic==Expected,TEXT("Music route matches actual scene or ending: ")+Expected.ToString()))return false;
            if(!Menu)
            {
                const int32 Starts=Audio->MusicStartCount(),Bells=Audio->EventCount(TEXT("Customer_Arrive")),History=Audio->EventCount(TEXT("History_Found"));
                IShopView::Execute_RefreshShop(Audio,S);
                if(!Report.Check(Audio->MusicStartCount()==Starts && Audio->EventCount(TEXT("Customer_Arrive"))==Bells && Audio->EventCount(TEXT("History_Found"))==History,
                    TEXT("Repeated state refresh does not restart music, arrival bell or fragment sting")))return false;
            }
            return true;
        }

        bool AudioEdges()
        {
            if(!Tutorial() || !WaitForArrival())return false;
            UShopAudioComponent* Audio=Controller->ShopAudio;
            const int32 Timeout=Audio->EventCount(TEXT("Customer_Timeout"));
            const int32 Index=UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get());
            Run->Tick(Run->GetCustomers_Implementation()[Index].Patience+.01f);
            if(!Report.Check(Audio->EventCount(TEXT("Customer_Timeout"))==Timeout+1,TEXT("Real patience expiry emits exactly one timeout sound")))return false;
            IShopView::Execute_RefreshShop(Audio,Snapshot()); Run->Tick(0.f);
            if(!Report.Check(Audio->EventCount(TEXT("Customer_Timeout"))==Timeout+1,TEXT("Timeout feedback is not repeated by zero tick or UI refresh")))return false;
            const int32 Purchases=Audio->EventCount(TEXT("Purchase_Normal")),Errors=Audio->EventCount(TEXT("Sale_Wrong"));
            Run->RequestRestock_Implementation(TEXT("book_novel"));
            UShopAudioLibrary::AfterUICommand(Root.Get(),TEXT("RequestRestock"));
            if(!Report.Check(!Run->GetLastResult().bSucceeded && Audio->EventCount(TEXT("Purchase_Normal"))==Purchases &&
                Audio->EventCount(TEXT("Sale_Wrong"))==Errors+1,TEXT("Failed purchase emits error feedback and never a purchase sound")))return false;
            UButton* Hover=Child<UButton>(Root.Get(),TEXT("BtnDecrees"));
            const int32 BeforeHover=Audio->EventCount(TEXT("UI_Hover"));
            Hover->OnHovered.Broadcast(); Hover->OnHovered.Broadcast();
            if(!Report.Check(Audio->EventCount(TEXT("UI_Hover"))==BeforeHover+1,TEXT("Real saved hover hook dispatches once and throttles immediate repetition")))return false;
            Audio->SetMuted(true); Audio->SetMixLevels(-2.f,2.f,1.f,1.f); Audio->SetMuted(false); Audio->SetMixLevels(1.f,1.f,1.f,1.f);
            return VerifyAudioScene();
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
            const int32 BellsBefore=Controller->ShopAudio->EventCount(TEXT("Customer_Arrive"));
            const bool WasPresent=Run->IsCurrentCustomerPresent();
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
            if(!Report.Check(Controller->ShopAudio->EventCount(TEXT("Customer_Arrive"))-BellsBefore==(WasPresent?0:1),TEXT("Arrival timer plays one doorbell only when the visitor becomes present")))return false;
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
            return Report.Check(Snapshot().PsychicMax == 0, TEXT("Snapshot exposes no gameplay psychic cap")) &&
                TextEquals(Root.Get(), TEXT("HUD_Psychic"), FText::FromString(FString::FromInt(Snapshot().Psychic)));
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
            UImage* PsychicArt=Child<UImage>(Root.Get(),TEXT("HUD_PsychicArt"));
            UCanvasPanelSlot* PsychicArtSlot=PsychicArt?Cast<UCanvasPanelSlot>(PsychicArt->Slot):nullptr;
            const FBox2f PsychicUV=PsychicArt?FBox2f(PsychicArt->Brush.GetUVRegion()):FBox2f(ForceInit);
            if (!Report.Check(PsychicArtSlot && PsychicUV.bIsValid && PsychicUV.Min.Equals(FVector2f::ZeroVector) && PsychicUV.Max.Equals(FVector2f(1,1)) &&
                PsychicArt->Brush.GetImageSize().Equals(FVector2D(146,46)) && PsychicArtSlot->GetPosition().Equals(FVector2D(726,3)) &&
                PsychicArtSlot->GetSize().Equals(FVector2D(219,69)) && !Root->WidgetTree->FindWidget(TEXT("PsychicIcon")) &&
                !Root->WidgetTree->FindWidget(TEXT("PsychicTitle")),TEXT("Psychic artwork retains the complete original icon, caption, background and geometry without cropped overlays"))) return false;
            for (const TCHAR* Field:{TEXT("Day"),TEXT("Money"),TEXT("Stock"),TEXT("Psychic"),TEXT("Pollution"),TEXT("Enlighten"),TEXT("Queue")})
            {
                UTextBlock* Value=Child<UTextBlock>(Root.Get(),FName(*(FString(TEXT("HUD_"))+Field)));
                UScaleBox* Fit=Value?Cast<UScaleBox>(Value->GetParent()):nullptr;
                UCanvasPanelSlot* Box=Fit?Cast<UCanvasPanelSlot>(Fit->Slot):nullptr;
                UScaleBoxSlot* TextSlot=Value?Cast<UScaleBoxSlot>(Value->Slot):nullptr;
                const bool Psychic=FString(Field)==TEXT("Psychic");
                const bool Geometry=Box && (Psychic ? Box->GetPosition().Equals(FVector2D(837,6)) && Box->GetSize().Equals(FVector2D(102,46.5)) :
                    FMath::IsNearlyEqual(Box->GetPosition().Y,9.f) && FMath::IsNearlyEqual(Box->GetSize().Y,55.f));
                if (!Report.Check(Box&&TextSlot&&TextSlot->GetHorizontalAlignment()==HAlign_Center&&TextSlot->GetVerticalAlignment()==VAlign_Center&&
                    Geometry,TEXT("HUD values are centered within their original display fields"))) return false;
            }
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
            const float CardHeight = bHasUnlist ? 686.f : 570.f;
            const float FooterHeight = bHasUnlist ? 208.f : 92.f;
            if (!Report.Check(Size && Paper && Canvas && Actions && Primary && Feedback && Description &&
                FMath::IsNearlyEqual(Size->GetWidthOverride(), 380.f) && FMath::IsNearlyEqual(Size->GetHeightOverride(), CardHeight) &&
                Paper->GetParent() == Size && Canvas->GetParent() == Paper && Actions->GetParent() == Canvas &&
                Primary->GetParent() == Actions && Feedback->GetParent() == Actions && Description->GetParent() == Canvas &&
                HasFixedCanvasRect(Actions, FVector2D(0, 446), FVector2D(344, FooterHeight)) &&
                HasFixedCanvasRect(Primary, FVector2D(0, 0), FVector2D(344, 48)) &&
                HasFixedCanvasRect(Description, FVector2D(0, 280), FVector2D(344, 96)) &&
                HasFixedCanvasRect(Feedback, FVector2D(0, bHasUnlist ? 168 : 54), FVector2D(344, bHasUnlist ? 40 : 38)),
                TEXT("Actual card has an independent scrolling description and fixed primary-action/footer rectangles: ") + GetNameSafe(Card))) return false;
            if (!Report.Check(bHasUnlist ? (Unlist && Unlist->GetParent() == Actions &&
                    HasFixedCanvasRect(Unlist, FVector2D(0, 56), FVector2D(344, 48))) : Unlist == nullptr,
                TEXT("Only secret-management cards contain the separated unlist button"))) return false;
            if (bHasUnlist && !Report.Check(HasFixedCanvasRect(Child<UButton>(Card,TEXT("BtnRead")),FVector2D(0,112),FVector2D(344,48)),
                TEXT("Reading button is aligned in each secret card footer"))) return false;
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

        bool VerifyGameGuideArtwork()
        {
            UUserWidget* Shop=Page(TEXT("ShopPage"));
            UImage* Owl=Child<UImage>(Shop,TEXT("MailboxOwlPortrait"));
            UButton* Hit=Child<UButton>(Shop,TEXT("BtnOwlGuide"));
            UTexture2D* Texture=Owl?Cast<UTexture2D>(Owl->Brush.GetResourceObject()):nullptr;
            if (!Report.Check(Texture && Texture->Source.GetSizeX()==512 && Texture->Source.GetSizeY()==512 &&
                Texture->GetName()==TEXT("T_OwlPerched") && Hit && Hit->OnClicked.IsBound() && Hit->OnHovered.IsBound(),TEXT("Mailbox owl uses the original transparent portrait and a saved interactive button"))) return false;
            if (!Report.Check(ShopUIPreview::Save(Root.Get(),TEXT("owl_mailbox_scene")),TEXT("Render owl standing on mailbox at 1080p")) ||
                !Report.Check(ShopUIPreview::Save(Root.Get(),TEXT("owl_mailbox_scene_720p"),FIntPoint(1280,720)),TEXT("Render mailbox at 720p"))) return false;
            if (!Report.Check(Controller->GameGuideWidgetClass!=nullptr,TEXT("Saved controller references editable WBP_GameGuide"))) return false;
            TStrongObjectPtr<UShopGameGuideWidget> Guide(CreateWidget<UShopGameGuideWidget>(Controller,Controller->GameGuideWidgetClass));
            if (!Report.Check(Guide.IsValid(),TEXT("Create actual saved game guide class"))) return false;
            const TSharedRef<SWidget> SlateGuide=Guide->TakeWidget();
            if (!Report.Check(Run->IsRealtimePaused() && !Root->GetIsEnabled(),TEXT("Guide lifecycle pauses realtime and blocks background UI"))) return false;
            UWidgetSwitcher* Tabs=Child<UWidgetSwitcher>(Guide.Get(),TEXT("GuidePages"));
            if (!Report.Check(Tabs && Tabs->GetChildrenCount()==2 && Tabs->GetActiveWidgetIndex()==0,TEXT("Guide has separate endings and pollution tabs"))) return false;
            for(int32 Section=0;Section<10;++Section)
            {
                UTextBlock* Title=Child<UTextBlock>(Guide.Get(),FName(*FString::Printf(TEXT("GuideSection%dTitle"),Section)));
                UTextBlock* Body=Child<UTextBlock>(Guide.Get(),FName(*FString::Printf(TEXT("GuideSection%dBody"),Section)));
                if (!Report.Check(Title && Body && Title->TextDelegate.IsBound() && Body->TextDelegate.IsBound() &&
                    !Title->TextDelegate.Execute().IsEmpty() && !Body->TextDelegate.Execute().IsEmpty(),FString::Printf(TEXT("Guide section %d reads current data through saved bindings"),Section))) return false;
            }
            if (!Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_endings")),TEXT("Render ending conditions")) ||
                !Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_endings_720p"),FIntPoint(1280,720)),TEXT("Render ending text at 720p"))) return false;
            Child<UScrollBox>(Guide.Get(),TEXT("EndingsScroll"))->ScrollToEnd();
            if (!Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_endings_bottom")),TEXT("Render true and redeemed endings at scroll bottom"))) return false;
            Child<UButton>(Guide.Get(),TEXT("BtnPollution"))->OnClicked.Broadcast();
            if (!Report.Check(Tabs->GetActiveWidgetIndex()==1,TEXT("Actual law tab button switches content while paused")) ||
                !Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_pollution")),TEXT("Render pollution and decree explanation")) ||
                !Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_pollution_720p"),FIntPoint(1280,720)),TEXT("Render pollution explanation at 720p"))) return false;
            Child<UScrollBox>(Guide.Get(),TEXT("PollutionScroll"))->ScrollToEnd();
            if (!Report.Check(ShopUIPreview::Save(Guide.Get(),TEXT("owl_guide_decrees_bottom")),TEXT("Render all seven decree summaries through scroll area"))) return false;
            Child<UButton>(Guide.Get(),TEXT("BtnClose"))->OnClicked.Broadcast();
            return Report.Check(!Run->IsRealtimePaused() && Root->GetIsEnabled(),TEXT("Saved close button releases pause and restores background interaction"));
        }

        bool VerifyMainMenuArtwork()
        {
            UUserWidget* Menu=Page(TEXT("MenuPage"));
            UImage* Background=Child<UImage>(Menu,TEXT("Background"));
            UTexture2D* Composition=Background?Cast<UTexture2D>(Background->Brush.GetResourceObject()):nullptr;
            if (!Report.Check(Composition && Composition->GetPathName()==TEXT("/Game/ProgramA/UI/Art/MainMenu/T_MainMenuComposition.T_MainMenuComposition") &&
                Composition->Source.GetSizeX()==1920 && Composition->Source.GetSizeY()==1080 && !Composition->VirtualTextureStreaming &&
                Background->GetVisibility()==ESlateVisibility::HitTestInvisible,
                TEXT("Main menu uses the full unmodified 1920x1080 supplied composition without blocking clicks"))) return false;
            const TCHAR* Names[]={TEXT("BtnStart"),TEXT("BtnQuit")};
            const TCHAR* ArtNames[]={TEXT("T_MainMenuStart"),TEXT("T_MainMenuQuit")};
            const FVector2D Positions[]={FVector2D(1283,369),FVector2D(1359,593)};
            const FVector2D Sizes[]={FVector2D(317,112),FVector2D(317,113)};
            for (int32 I=0;I<2;++I)
            {
                UButton* Button=Child<UButton>(Menu,Names[I]);
                UCanvasPanelSlot* Slot=Button?Cast<UCanvasPanelSlot>(Button->Slot):nullptr;
                UTexture2D* Art=Button?Cast<UTexture2D>(Button->WidgetStyle.Hovered.GetResourceObject()):nullptr;
                if (!Report.Check(Button && Slot && Slot->GetPosition().Equals(Positions[I]) && Slot->GetSize().Equals(Sizes[I]) &&
                    Button->GetContent()==nullptr && Button->WidgetStyle.Normal.DrawAs==ESlateBrushDrawType::NoDrawType &&
                    Art && Art->GetName()==ArtNames[I] && Button->GetIsEnabled() && Button->OnClicked.IsBound() && Button->OnHovered.IsBound(),
                    FString::Printf(TEXT("Original %s button keeps its live click/audio delegates and exact supplied image hit area"),Names[I]))) return false;
            }
            for (const TCHAR* Name:{TEXT("PageShade"),TEXT("HeadingPanel"),TEXT("PageTitle"),TEXT("PageSubtitle"),TEXT("MenuPanel"),TEXT("MenuStory"),TEXT("Feedback")})
            {
                UWidget* Old=Menu->WidgetTree->FindWidget(Name);
                if (!Report.Check(Old && Old->GetVisibility()==ESlateVisibility::Collapsed,TEXT("Generated menu overlay is hidden: ")+FString(Name))) return false;
            }
            return Report.Check(ShopUIPreview::Save(Root.Get(),TEXT("main_menu_artwork")),TEXT("Render supplied menu at 1080p")) &&
                Report.Check(ShopUIPreview::Save(Root.Get(),TEXT("main_menu_artwork_720p"),FIntPoint(1280,720)),TEXT("Render supplied menu at 720p"));
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
            const int32 BeforeSales = Snapshot().TotalSold;
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
            if (!Report.Check(Snapshot().Money == BeforeMoney + Book.Price && AfterBook.Stock == BeforeBook.Stock - 1 && Snapshot().TotalSold == BeforeSales + 1,
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
                Snapshot().Enlighten == BeforeState.Enlighten + 5 &&
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
            if (!SelectDecree(CalmState.DecreeCandidates[Candidate],true) || !Click(Page(TEXT("DecreePage")),TEXT("BtnConfirm")) ||
                !At(EGamePhase::Day, 2, TEXT("Actual decree click resumes daytime trading"))) return false;
            return Report.Check(Snapshot().Pollution < CalmState.Pollution && Snapshot().TotalSold == CalmState.TotalSold &&
                Snapshot().Money <= CalmState.Money, TEXT("Decree applies its actual cost/effect without replaying the sale"));
        }

        bool SelectDecree(FName Id,bool InspectDetails=false)
        {
            UUserWidget* Decree=Page(TEXT("DecreePage"));
            UUserWidget* Introduction=Child<UUserWidget>(Decree,TEXT("IntroductionPage"));
            UWidgetSwitcher* Switcher=Child<UWidgetSwitcher>(Decree,TEXT("DecreePages"));
            const FRunSnapshot Before=Snapshot(); const TArray<FCustomerRuntime> Queue=Run->GetCustomers_Implementation();
            const FName Button=Id==TEXT("emergency_calm")?FName(TEXT("BtnEmergency")):FName(*(TEXT("BtnSelect_")+Id.ToString()));
            if(!Click(Decree,Button))return false;
            const FNameProperty* Selected=FindFProperty<FNameProperty>(Decree->GetClass(),TEXT("SelectedDecreeId"));
            UImage* Preview=Child<UImage>(Decree,TEXT("SelectedPreview"));
            const FString TextureName=Id==TEXT("emergency_calm")?TEXT("T_Decree_Bronze"):TEXT("T_Decree_")+Id.ToString();
            if(!Report.Check(Selected && Selected->GetPropertyValue_InContainer(Decree)==Id && Switcher && Switcher->GetActiveWidgetIndex()==0 &&
                Preview && Preview->BrushDelegate.IsBound(),TEXT("Card click selects its stable ID and binds the right-hand preview")))return false;
            UTexture2D* Artwork=Cast<UTexture2D>(Preview->BrushDelegate.Execute().GetResourceObject());
            if(!Report.Check(Artwork && Artwork->GetPathName()==ObjectPath(AssetRoot+TEXT("Art/Decrees/")+TextureName) &&
                Artwork->Source.GetSizeX()==256 && Artwork->Source.GetSizeY()==512,TEXT("Preview uses the supplied full-size artwork for ")+Id.ToString()))return false;
            for(FName Law:Run->GetDecreeIds())
            {
                UBorder* Border=Child<UBorder>(Decree,FName(*(TEXT("SelectionBorder_")+Law.ToString())));
                if(!Report.Check(Border && Border->BrushColorDelegate.IsBound(),TEXT("Selection border is bound to stable ID: ")+Law.ToString()))return false;
                const FLinearColor Color=Border->BrushColorDelegate.Execute();
                if(!Report.Check(Law==Id ? Color.R>.9f && Color.G<.1f && Color.A==1.f : Color.A==0.f,TEXT("Only selected decree has a red border: ")+Law.ToString()))return false;
            }
            UButton* Confirm=Child<UButton>(Decree,TEXT("BtnConfirm")); UButton* Info=Child<UButton>(Decree,TEXT("BtnIntroduction"));
            FText Reason; const bool CanEnact=Run->CanEnactDecree(Id,Reason);
            if(!Report.Check(Confirm && Info && Confirm->bIsEnabledDelegate.IsBound() && Info->bIsEnabledDelegate.IsBound() &&
                Confirm->bIsEnabledDelegate.Execute()==CanEnact && Info->bIsEnabledDelegate.Execute(),TEXT("Introduction remains available; confirm follows actual candidate, cost and cooldown rules")) ||
                !TextEquals(Decree,TEXT("SelectedTitle"),UShopPresentationLibrary::GetDecreeNameById(Root.Get(),Id)))return false;
            if(InspectDetails)
            {
                if(bCapturePreviews && !Report.Check(ShopUIPreview::Save(Root.Get(),PreviewPrefix+TEXT("_decree_selected_")+Id.ToString()),TEXT("Render selection page with original supplied components")))return false;
                if(!Click(Decree,TEXT("BtnIntroduction")) || !Report.Check(Introduction && Switcher->GetActiveWidgetIndex()==1 && Snapshot().Phase==EGamePhase::Calm,
                    TEXT("Introduction opens as a separate WBP while business stays paused")))return false;
                UImage* Left=Child<UImage>(Introduction,TEXT("IntroductionArtwork"));
                if(!Report.Check(Left && Left->BrushDelegate.IsBound() && Left->BrushDelegate.Execute().GetResourceObject()==Artwork,TEXT("Introduction left artwork matches selected right preview")) ||
                    !TextEquals(Introduction,TEXT("DescriptionBody"),UShopPresentationLibrary::GetDecreeDescriptionById(Root.Get(),Id)) ||
                    !TextEquals(Introduction,TEXT("DecreeStatus"),UShopPresentationLibrary::GetDecreeStatusText(Root.Get())) ||
                    !TextEquals(Introduction,TEXT("BacklashLog"),UShopPresentationLibrary::GetBacklashLogText(Root.Get())))return false;
                UScrollBox* Scroll=Child<UScrollBox>(Introduction,TEXT("IntroductionScroll"));
                if(!Report.Check(Scroll && FMath::IsNearlyZero(Scroll->GetScrollOffset()),TEXT("Opening an introduction starts its description at the top")))return false;
                Run->Tick(90.f);
                if(bCapturePreviews && !Report.Check(ShopUIPreview::Save(Root.Get(),PreviewPrefix+TEXT("_decree_introduction_")+Id.ToString()),TEXT("Render left artwork and right effect description")))return false;
                if(!Report.Check(FMath::IsNearlyZero(Scroll->GetScrollOffset()),TEXT("Description remains at the top after real Slate construction and rendering")))return false;
                Scroll->SetScrollOffset(10000.f);
                if(bCapturePreviews && Id==TEXT("bronze_01") && !Report.Check(ShopUIPreview::Save(Root.Get(),PreviewPrefix+TEXT("_decree_introduction_scrolled")),TEXT("Render scrollable status and backlash records")))return false;
                if(!Click(Introduction,TEXT("BtnBack")) || !Report.Check(Switcher->GetActiveWidgetIndex()==0 && Selected->GetPropertyValue_InContainer(Decree)==Id,
                    TEXT("Introduction back returns to selection and retains the same chosen card")))return false;
            }
            const FRunSnapshot After=Snapshot(); const auto AfterQueue=Run->GetCustomers_Implementation();
            if(!Report.Check(After.Phase==Before.Phase && After.Money==Before.Money && After.Psychic==Before.Psychic && After.Pollution==Before.Pollution &&
                After.Turn==Before.Turn && After.ActiveDecrees.Num()==Before.ActiveDecrees.Num() && After.DecreeCandidates==Before.DecreeCandidates &&
                After.bCustomerPresent==Before.bCustomerPresent && After.CustomerArrivalRemaining==Before.CustomerArrivalRemaining && AfterQueue.Num()==Queue.Num(),
                TEXT("Selection and introduction charge nothing, draw nothing, enact nothing and freeze arrivals")))return false;
            for(int32 I=0;I<Queue.Num();++I)
                if(!Report.Check(AfterQueue[I].Patience==Queue[I].Patience,TEXT("Introduction freezes every queued customer's patience")))return false;
            return true;
        }

        bool DecreeSelectionReset()
        {
            UUserWidget* Decree=Page(TEXT("DecreePage"));
            const FNameProperty* Id=FindFProperty<FNameProperty>(Decree->GetClass(),TEXT("SelectedDecreeId"));
            UWidgetSwitcher* Switcher=Child<UWidgetSwitcher>(Decree,TEXT("DecreePages"));
            UButton* Confirm=Child<UButton>(Decree,TEXT("BtnConfirm")); UButton* Intro=Child<UButton>(Decree,TEXT("BtnIntroduction"));
            return Report.Check(Id && Id->GetPropertyValue_InContainer(Decree).IsNone() && Switcher && Switcher->GetActiveWidgetIndex()==0 &&
                Confirm && Intro && !Confirm->bIsEnabledDelegate.Execute() && !Intro->bIsEnabledDelegate.Execute() &&
                EffectiveVisibility(Child<UWidget>(Decree,TEXT("NoSelectionLabel")))==ESlateVisibility::HitTestInvisible &&
                EffectiveVisibility(Child<UWidget>(Root.Get(),TEXT("HUD")))==ESlateVisibility::Collapsed,
                TEXT("Fresh entry clears stale selection/detail, disables both actions, and keeps the supplied composition unobscured"));
        }

        bool ManualDecreePause()
        {
            const float Arrival=Snapshot().CustomerArrivalRemaining;
            if(!Click(Root.Get(),TEXT("BtnDecrees")) || !At(EGamePhase::Calm,8,TEXT("Manual decree opened before customer arrival")))return false;
            Run->Tick(90.f);
            if(!Report.Check(!Snapshot().bCustomerPresent && FMath::IsNearlyEqual(Arrival,Snapshot().CustomerArrivalRemaining),TEXT("Real decree button freezes arrival clock")))return false;
            if(!DecreeSelectionReset())return false;
            for(FName Id:Run->GetDecreeIds())
                if(!SelectDecree(Id,true))return false;
            if(!SelectDecree(TEXT("emergency_calm"),true))return false;
            if(!Click(Page(TEXT("DecreePage")),TEXT("BtnSkip")) || !At(EGamePhase::Day,2,TEXT("Manual close resumes waiting")) || !SelectVisitor())return false;
            const int32 Index=UShopPresentationLibrary::GetCurrentCustomerIndex(Root.Get());
            const float Patience=Run->GetCustomers_Implementation()[Index].Patience;
            if(!Click(Root.Get(),TEXT("BtnDecrees")) || !DecreeSelectionReset())return false; Run->Tick(90.f);
            if(!Report.Check(FMath::IsNearlyEqual(Run->GetCustomers_Implementation()[Index].Patience,Patience),TEXT("Real decree button freezes selected customer's patience")))return false;
            const FRunSnapshot BeforeEmergency=Snapshot();
            if(!SelectDecree(TEXT("emergency_calm"),true) || !Click(Page(TEXT("DecreePage")),TEXT("BtnConfirm")) ||
                !Report.Check(Snapshot().Phase==EGamePhase::Day && Snapshot().Money==BeforeEmergency.Money-Run->GetRunRules().EmergencyMoneyCost &&
                Snapshot().Pollution==FMath::Max(0,BeforeEmergency.Pollution-Run->GetRunRules().EmergencyPollutionCut),TEXT("Confirm applies emergency cost once and resumes daytime")))return false;
            if(!Report.Check(IsVisibleThroughParents(Child<UWidget>(Page(TEXT("ShopPage")),TEXT("InteractionPanel"))),TEXT("Closing decree preserves the already selected customer panel")))return false;
            Run->Tick(.5f);
            return Report.Check(Run->GetCustomers_Implementation()[Index].Patience<Patience,TEXT("Patience runs again after closing"));
        }

        bool HistoryFlow(bool ExpectDrop)
        {
            if(!Click(Page(TEXT("NightChoicePage")),TEXT("BtnInside")) || !At(EGamePhase::Inside,7,TEXT("Inside includes per-book reading buttons")))return false;
            if(!Click(Root.Get(),TEXT("BtnDecrees")) || !At(EGamePhase::Calm,8,TEXT("Manual decrees from inside")) ||
                !Click(Page(TEXT("DecreePage")),TEXT("BtnSkip")) || !At(EGamePhase::Inside,7,TEXT("Close decree restores inside")))return false;
            TSet<FName> Found; int32 Reads=0;
            for(FName Id:Run->GetBookIds())
            {
                FBookData Data; int32 Stock=0; Run->GetBookInfo_Implementation(Id,Data,Stock);
                if(Data.BookType!=EBookType::Secret || Stock<=0)continue;
                UUserWidget* Card=Child<UUserWidget>(Page(TEXT("InsidePage")),FName(*(TEXT("Card_")+Id.ToString())));
                const FRunSnapshot Before=Snapshot();
                if(!Click(Card,TEXT("BtnRead")))return false; ++Reads;
                if(!Report.Check(Snapshot().Psychic==Before.Psychic+10 && Snapshot().Pollution==Before.Pollution+10,
                    TEXT("Actual read button grants ten psychic and ten base pollution")))return false;
                if(!Report.Check(Snapshot().Enlighten==Before.Enlighten+(ExpectDrop?10:0),TEXT("New page grants ten enlightenment; probability miss grants zero")))return false;
                if(ExpectDrop)
                {
                    if(!At(EGamePhase::History,11,TEXT("Actual read opens full history page")))return false;
                    const FName PageId=Snapshot().PendingEventId; FEventData Event;
                    if(!Report.Check(!Found.Contains(PageId)&&Run->GetEventInfo(PageId,Event),TEXT("Page is new and resolves to authored event")))return false;
                    Found.Add(PageId);
                    if(!TextEquals(Page(TEXT("HistoryPage")),TEXT("PageTitle"),Event.Title) ||
                        !TextEquals(Page(TEXT("HistoryPage")),TEXT("HistoryBody"),Event.Text))return false;
                    UScrollBox* Scroll=Child<UScrollBox>(Page(TEXT("HistoryPage")),TEXT("HistoryScroll"));
                    if(!Report.Check(Scroll&&FMath::IsNearlyZero(Scroll->GetScrollOffset()),TEXT("Each newly found page starts at the top")))return false;
                    if(bCapturePreviews)
                    {
                        Scroll->SetScrollOffset(10000.f);
                        if(!Report.Check(ShopUIPreview::Save(Root.Get(),TEXT("history_bottom_")+PageId.ToString()),TEXT("Render last paragraphs through real scroll container")))return false;
                    }
                    if(!Click(Page(TEXT("HistoryPage")),TEXT("BtnDone")))return false;
                }
                else if(!Report.Check(Snapshot().CollectedHistoryPages.IsEmpty()&&Snapshot().Phase==EGamePhase::Inside,TEXT("Probability miss grants psychic without a history modal")))return false;
                if(Snapshot().Phase==EGamePhase::Calm)
                {
                    if(!At(EGamePhase::Calm,8,TEXT("Pending threshold modal follows the fragment")) || !Click(Page(TEXT("DecreePage")),TEXT("BtnSkip")))return false;
                }
                if(!Report.Check(Snapshot().Psychic==Before.Psychic+10 && Snapshot().Pollution==Before.Pollution+10,
                    TEXT("Closing all modals does not award or charge a second time")))return false;
                if(!Report.Check(Snapshot().Enlighten==Before.Enlighten+(ExpectDrop?10:0),TEXT("Closing page/decrees cannot duplicate the enlightenment reward")))return false;
                UButton* Read=Child<UButton>(Card,TEXT("BtnRead"));
                if(!Report.Check(Read && Read->bIsEnabledDelegate.IsBound() && !Read->bIsEnabledDelegate.Execute(),TEXT("Already read title button is disabled tonight")))return false;
                if(!TextEquals(Card,TEXT("BtnRead_Caption"),FText::FromString(TEXT("今晚已翻阅"))))return false;
                if(!ExpectDrop)break;
            }
            return Report.Check(Reads==(ExpectDrop?7:1) && Found.Num()==(ExpectDrop?7:0),TEXT("Real card flows cover all seven original pages or the probability-miss branch"));
        }

        bool DecreeLifetimeFlow()
        {
            if(!Click(Page(TEXT("NightChoicePage")),TEXT("BtnInside")))return false;
            int32 ReadCount=0;
            for(FName Id:Run->GetBookIds())
            {
                FBookData Book; int32 Stock; Run->GetBookInfo_Implementation(Id,Book,Stock);
                if(Book.BookType!=EBookType::Secret || Stock<=0)continue;
                if(!Click(Child<UUserWidget>(Page(TEXT("InsidePage")),FName(*(TEXT("Card_")+Id.ToString()))),TEXT("BtnRead")))return false;
                if(++ReadCount==4)break;
            }
            if(!At(EGamePhase::Calm,8,TEXT("Four reads unlock actual bronze laws")))return false;
            const FName Quiet(TEXT("bronze_01")); const int32 Candidate=Snapshot().DecreeCandidates.IndexOfByKey(Quiet);
            if(!Report.Check(Candidate!=INDEX_NONE,TEXT("Authored quiet decree is available in light stage")))return false;
            if(!SelectDecree(Quiet,true) || !Click(Page(TEXT("DecreePage")),TEXT("BtnConfirm")) || !Click(Root.Get(),TEXT("BtnDecrees")))return false;
            PreviewPrefix=TEXT("decree_lifetime");
            if(!At(EGamePhase::Calm,8,TEXT("Reopen shows active lifetime")) ||
                !Report.Check(UShopPresentationLibrary::GetDecreeStatusText(Root.Get()).ToString().Contains(TEXT("生效中，到反噬剩余 2 回合")),TEXT("UI shows two real remaining turns")) ||
                !Click(Page(TEXT("DecreePage")),TEXT("BtnSkip")) || !Click(Page(TEXT("InsidePage")),TEXT("BtnDone")) ||
                !Click(Page(TEXT("NightEndPage")),TEXT("BtnContinue")) || !ServeDay())return false;
            if(!Click(Page(TEXT("NightChoicePage")),TEXT("BtnInside")) || !Click(Page(TEXT("InsidePage")),TEXT("BtnDone")) ||
                !Click(Page(TEXT("NightEndPage")),TEXT("BtnContinue")) || !Click(Root.Get(),TEXT("BtnDecrees")))return false;
            if(!At(EGamePhase::Calm,8,TEXT("Two settlements trigger backlash and cooldown")))return false;
            if(!Report.Check(Snapshot().DecreeBacklashLog.Num()==1 && UShopPresentationLibrary::GetDecreeStatusText(Root.Get()).ToString().Contains(TEXT("冷却中，剩余 3 回合")),
                TEXT("Real decree cooldown and exactly one backlash log shown")))return false;
            return SelectDecree(Quiet,true) && TextEquals(Child<UUserWidget>(Page(TEXT("DecreePage")),TEXT("IntroductionPage")),TEXT("BacklashLog"),UShopPresentationLibrary::GetBacklashLogText(Root.Get())) &&
                TextEquals(Root.Get(),TEXT("LatestBacklash"),UShopPresentationLibrary::GetLatestBacklashText(Root.Get()));
        }

        bool BlackMarketFlow()
        {
            bCapturePreviews=false;
            for(int32 Day=1;Day<=7;++Day)
            {
                for(int32 Visitor=0;Visitor<3;++Visitor)if(!SellCurrentOrdinary(false))return false;
                if(!Click(Page(TEXT("DayEndPage")),TEXT("BtnContinue")))return false;
                UButton* Option=Child<UButton>(Page(TEXT("NightChoicePage")),TEXT("BtnBlackMarket"));
                if(!Report.Check(Option && (EffectiveVisibility(Option)==ESlateVisibility::Visible)==(Day==7),TEXT("Black market option appears only on the seventh night")))return false;
                if(Day==7)break;
                if(!Click(Page(TEXT("NightChoicePage")),TEXT("BtnMerchant")) || !RefillOrdinary(false) ||
                    !Click(Page(TEXT("MerchantPage")),TEXT("BtnDone")) || !Click(Page(TEXT("NightEndPage")),TEXT("BtnContinue")))return false;
            }
            bCapturePreviews=true; PreviewPrefix=TEXT("weekly_market");
            if(!At(EGamePhase::DuskChoice,5,TEXT("Seventh night has third choice")) || !Click(Page(TEXT("NightChoicePage")),TEXT("BtnBlackMarket")) ||
                !At(EGamePhase::Market,12,TEXT("Black market enters merchant scene first")))return false;
            UUserWidget* Market=Page(TEXT("BlackMarketPage")); UWidget* Panel=Child<UWidget>(Market,TEXT("PurchasePanel"));
            if(!Report.Check(Panel && EffectiveVisibility(Panel)==ESlateVisibility::Collapsed,TEXT("Black market purchase panel starts hidden")))return false;
            const UImage* Portrait=Child<UImage>(Market,TEXT("MerchantPortrait"));
            const UImage* Original=Child<UImage>(Page(TEXT("MerchantPage")),TEXT("MerchantPortrait"));
            if(!Report.Check(Portrait&&Original&&Portrait->Brush.GetResourceObject()==Original->Brush.GetResourceObject(),TEXT("Black market reuses supplied merchant portrait")))return false;
            if(!Click(Market,TEXT("BtnMerchantHit")) || !At(EGamePhase::Market,12,TEXT("Merchant click reveals secret books")))return false;
            const FName Id(TEXT("book_secret_01")); FMarketItemData Item; FBookRuntime Before,After;
            if(!Report.Check(Run->GetMarketItemInfo(Id,Item)&&Run->GetBookRuntime(Id,Before),TEXT("Market book card links to real inventory by ID")))return false;
            UUserWidget* Card=Child<UUserWidget>(Market,TEXT("Card_book_secret_01")); const auto StateBefore=Snapshot();
            if(!TextEquals(Card,TEXT("BookDescription"),UShopPresentationLibrary::GetMarketBookDescription(Root.Get(),Id)) || !Click(Card,TEXT("BtnPrimary")))return false;
            Run->GetBookRuntime(Id,After);
            if(!Report.Check(After.Stock==Before.Stock+1&&After.StoredCopies==Before.StoredCopies+1&&After.ListedCopies==Before.ListedCopies&&
                Snapshot().Money==StateBefore.Money-Item.Price&&Snapshot().Pollution==StateBefore.Pollution+5,TEXT("Actual black market purchase pays, stores unlisted copy and adds five pollution")))return false;
            if(!Report.Check(EffectiveVisibility(Panel)==ESlateVisibility::SelfHitTestInvisible,TEXT("Inventory refresh leaves purchase panel open")))return false;
            UButton* Buy=Child<UButton>(Card,TEXT("BtnPrimary"));
            if(!Report.Check(Buy && !Buy->bIsEnabledDelegate.Execute(),TEXT("Bought offer disabled until next weekly market")))return false;
            if(!Click(Market,TEXT("BtnClosePurchase")) || !At(EGamePhase::Market,12,TEXT("Close purchase returns to same black market scene")) ||
                !Click(Market,TEXT("BtnDone")) || !At(EGamePhase::NightEnd,9,TEXT("Leaving black market settles night once")) ||
                !Click(Page(TEXT("NightEndPage")),TEXT("BtnContinue")) || !At(EGamePhase::Day,2,TEXT("Seventh-night market returns to eighth-day shop")))return false;
            return Report.Check(Snapshot().Day==8,TEXT("Market advances exactly one day"));
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
            FFormatNamedArguments EndingArgs;
            EndingArgs.Add(TEXT("Money"),FText::AsNumber(Snapshot().Money+Snapshot().RedemptionPaid,&FNumberFormattingOptions::DefaultNoGrouping()));
            EndingArgs.Add(TEXT("Days"),FText::AsNumber(Snapshot().MaxDays,&FNumberFormattingOptions::DefaultNoGrouping()));
            const FText ExpectedStory=Authored?FText::Format(Authored->Text,EndingArgs):FText::GetEmpty();
            if (!Report.Check(Authored && Snapshot().EndMessage.EqualTo(ExpectedStory),
                TEXT("Business ending text comes from the matching original DT_Endings row"))) return false;
            if (!TextEquals(Page(TEXT("EndingPage")), TEXT("EndingTitle"), Authored->Title) ||
                !TextEquals(Page(TEXT("EndingPage")), TEXT("EndingStory"), ExpectedStory) ||
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
            // subsequent nights restock the non-progressive book for the ordinary-sales strategy.
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

        bool FullConfiguredDays(bool bProgressive)
        {
            bCapturePreviews = false;
            if (!Report.Check(Snapshot().MaxDays == SavedRules.MaxDays && FixtureTables.IsEmpty(),
                TEXT("Full-term scenario uses exact saved data: configured days and zero fixture tables"))) return false;
            for (int32 Day = 1; Day <= SavedRules.MaxDays; ++Day)
            {
                if (!At(EGamePhase::Day, 2, TEXT("Full-term daily opening")) ||
                    !Report.Check(Snapshot().Day == Day && Snapshot().Ending == EShopEnding::None,
                        TEXT("Exactly one next day, no early final-threshold ending"))) return false;
                for (int32 Customer = 0; Customer < 3; ++Customer) if (!SellCurrentOrdinary(bProgressive)) return false;
                if (!At(EGamePhase::DayEnd, 4, TEXT("Full-term third visitor settles the day")) ||
                    !Click(Page(TEXT("DayEndPage")), TEXT("BtnContinue")) || !At(EGamePhase::DuskChoice, 5, TEXT("Full-term dusk choice"))) return false;
                UButton* MarketOption=Child<UButton>(Page(TEXT("NightChoicePage")),TEXT("BtnBlackMarket"));
                if (!Report.Check(MarketOption && (EffectiveVisibility(MarketOption)==ESlateVisibility::Visible)==(Day%SavedRules.DaysPerWeek==0),
                    FString::Printf(TEXT("Day %d black market visibility follows the weekly calendar, including the final night"),Day))) return false;
                if (!Click(Page(TEXT("NightChoicePage")), TEXT("BtnMerchant")) || !At(EGamePhase::Restock, 6, TEXT("Full-term merchant"))) return false;
                if (Day < SavedRules.MaxDays && !RefillOrdinary(bProgressive)) return false;
                if (Day == SavedRules.MaxDays && !VerifyMerchantScene()) return false;
                if (!Click(Page(TEXT("MerchantPage")), TEXT("BtnDone")) || !At(EGamePhase::NightEnd, 9, TEXT("Full-term completed night settlement"))) return false;
                Report.Note(FString::Printf(TEXT("FULL_CALENDAR day=%d seed-strategy=%s money=%d enlightenment=%d pollution=%d sold=%d"),
                    Day, bProgressive ? TEXT("progressive") : TEXT("ordinary"), Snapshot().Money,
                    Snapshot().Enlighten, Snapshot().Pollution, Snapshot().TotalSold));
                if (!Click(Page(TEXT("NightEndPage")), TEXT("BtnContinue"))) return false;
                if (Day < SavedRules.MaxDays && !Report.Check(Snapshot().Phase == EGamePhase::Day && Snapshot().Day == Day + 1 && Snapshot().Ending == EShopEnding::None,
                    TEXT("Before the configured final day the night-continue button opens the following day, not an ending"))) return false;
            }
            return At(EGamePhase::End, 10, TEXT("Full-term configured final continue opens ending")) &&
                Report.Check(Snapshot().Day == SavedRules.MaxDays && Snapshot().TotalSold == SavedRules.MaxDays*3,
                    TEXT("All real visitors resolved over the configured calendar; no extra day was created"));
        }

        bool BankruptcyByRejection()
        {
            bCapturePreviews = false;
            for (int32 Guard = 0; Guard < SavedRules.MaxDays && Snapshot().Phase != EGamePhase::End; ++Guard)
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
            if (!Report.Check(FixtureTables.IsEmpty() && Snapshot().NegativeDays >= 3 && Snapshot().Day < SavedRules.MaxDays,
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
            return Report.Check(Snapshot().Pollution >= SavedRules.PollutionLimit && Snapshot().MaxDays == SavedRules.MaxDays && Snapshot().Day == 2,
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
    if(FParse::Param(*Params,TEXT("GuideOnly")))
    {
        {
            FSession Guide(Report);
            if(bAssetsReady && Guide.Start(PC->GeneratedClass,Root->GeneratedClass,false))
                if(!Guide.Tutorial() || !Guide.VerifyGameGuideArtwork()) Report.Note(TEXT("Owl guide scenario stopped at first failure."));
        }
        Report.Note(FString::Printf(TEXT("OWL GUIDE RESULT checks=%d failures=%d"),Report.Checks,Report.Failures));
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("UIBuild/OwlGameGuide");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FFileHelper::SaveStringToFile(Report.Lines,*(Folder/TEXT("Verification.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        return Report.Failures?1:0;
    }
    if(FParse::Param(*Params,TEXT("MainMenuOnly")))
    {
        {
            FSession Menu(Report); Menu.PreviewPrefix=TEXT("main_menu_flow");
            if (bAssetsReady && Menu.Start(PC->GeneratedClass,Root->GeneratedClass,false))
            {
                if (!Menu.VerifyMainMenuArtwork() || !Menu.Tutorial()) Report.Note(TEXT("Main-menu scenario stopped at first failure."));
            }
        }
        Report.Note(FString::Printf(TEXT("MAIN MENU RESULT checks=%d failures=%d"),Report.Checks,Report.Failures));
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("UIBuild/MainMenuArtwork");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FFileHelper::SaveStringToFile(Report.Lines,*(Folder/TEXT("Verification.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        return Report.Failures?1:0;
    }
    if(FParse::Param(*Params,TEXT("AudioOnly")))
    {
        {
            FSession Audio(Report); Audio.bCapturePreviews=false;
            if(bAssetsReady && Audio.Start(PC->GeneratedClass,Root->GeneratedClass,false))Audio.AudioEdges();
        }
        Report.Note(FString::Printf(TEXT("AUDIO EDGE RESULT checks=%d failures=%d"),Report.Checks,Report.Failures));
        FFileHelper::SaveStringToFile(Report.Lines,*(FPaths::ProjectSavedDir()/TEXT("Audio/EdgeVerification.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        return Report.Failures?1:0;
    }
    if(bAssetsReady)
    {
        UDataTable* Events=Load<UDataTable>(AssetRoot+TEXT("Data/DT_Events_UI"));
        FString Json; TSharedPtr<FJsonObject> Document;
        const bool Read=FFileHelper::LoadFileToString(Json,*(FPaths::ProjectDir()/TEXT("SourceArt/History/history_fragments.json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Document) && Document.IsValid();
        const TArray<TSharedPtr<FJsonValue>>* Fragments=nullptr;
        bAssetsReady &= Report.Check(Events && Read && Document->TryGetArrayField(TEXT("fragments"),Fragments) && Fragments->Num()==7 && Events->GetRowNames().Num()==7,
            TEXT("Seven document sources and seven saved history rows"));
        if(bAssetsReady)for(const auto& Value:*Fragments)
        {
            const auto Page=Value->AsObject(); const FString Id=Page->GetStringField(TEXT("id"));
            const FEventData* Row=Events->FindRow<FEventData>(FName(*Id),TEXT("HistorySourceVerify"));
            bAssetsReady &= Report.Check(Row && Row->Title.ToString()==Page->GetStringField(TEXT("title")) && Row->Text.ToString()==Page->GetStringField(TEXT("text")),
                TEXT("Saved page exactly matches document paragraphs and masking: ")+Id);
        }
    }
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
        {
            FSession Layout(Report); Layout.bCaptureSmallPreviews = true;
            if (Layout.Start(PC->GeneratedClass, Root->GeneratedClass, true, false, 731, false, false, 0.f, 1234567890))
            {
                Layout.PreviewPrefix = TEXT("large_psychic_layout");
                if (!Layout.Tutorial() || !Layout.ServeDay() || !Layout.Night(true))
                    Report.Note(TEXT("Large psychic and small-screen layout scenario stopped at first failure."));
            }
        }
        {
            FSession Manual(Report); Manual.PreviewPrefix=TEXT("manual_decree");
            if(!Manual.Start(PC->GeneratedClass,Root->GeneratedClass,true) || !Manual.Tutorial() || !Manual.ManualDecreePause())
                Report.Note(TEXT("Manual decree UI scenario stopped at first failure."));
        }
        for(const float Chance:{0.f,1.f})
        {
            FSession History(Report);
            if(!History.Start(PC->GeneratedClass,Root->GeneratedClass,true,false,731,false,false,Chance) || !History.Tutorial() || !History.ServeDay() || !History.HistoryFlow(Chance>0.f))
                Report.Note(TEXT("History UI scenario stopped at first failure."));
        }
        {
            FSession Decree(Report);
            if(!Decree.Start(PC->GeneratedClass,Root->GeneratedClass,true,false,731,false,false,0.f) || !Decree.Tutorial() || !Decree.ServeDay() || !Decree.DecreeLifetimeFlow())
                Report.Note(TEXT("Decree lifetime UI scenario stopped at first failure."));
        }
        {
            FSession Market(Report); Market.bCapturePreviews=false;
            if(!Market.Start(PC->GeneratedClass,Root->GeneratedClass,false) || !Market.Tutorial() || !Market.BlackMarketFlow())
                Report.Note(TEXT("Weekly black market UI scenario stopped at first failure."));
        }
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

        Report.Note(TEXT("FULL THIRTY-FIVE DAYS: exact saved rules; real sales and restocking reach the final funding check."));
        {
            FSession Natural(Report); Natural.bCapturePreviews=false;
            if(Natural.Start(PC->GeneratedClass,Root->GeneratedClass,false)&&Natural.Tutorial()&&Natural.FullConfiguredDays(false))
            {
                Natural.PreviewPrefix=TEXT("full35_empty_shelf"); Natural.bCapturePreviews=true;
                Natural.VerifyEnding(EShopEnding::FailedRedemption,TEXT("Real saved 35-day ordinary-sales strategy ending"));
            }
        }
        Report.Note(TEXT("FIVE ENDINGS: explicitly labelled transient editor presets; actual saved choice/restart buttons and unmodified five-row manuscript table."));
        for(const EShopEndingTest Preset:{EShopEndingTest::EmptyShelf,EShopEndingTest::Pollution,EShopEndingTest::Closed,EShopEndingTest::Redeemed,EShopEndingTest::TruthChoice})
        {
            FSession Ending(Report); Ending.bCapturePreviews=false;
            if(!Ending.Start(PC->GeneratedClass,Root->GeneratedClass,false))continue;
            Ending.PreviewPrefix=TEXT("ending_")+StaticEnum<EShopEndingTest>()->GetNameStringByValue(static_cast<int64>(Preset));
            Ending.bCapturePreviews=true; Ending.bCaptureSmallPreviews=true;
            const FString TestCommand=TEXT("ShopTestEnding ")+StaticEnum<EShopEndingTest>()->GetNameStringByValue(static_cast<int64>(Preset));
            if(!Report.Check(Ending.Controller->ProcessConsoleExec(*TestCommand,*GLog,Ending.Controller),TEXT("Editor test console command works directly from the main menu")))continue;
            UUserWidget* Page=Ending.Page(TEXT("EndingPage"));
            UScrollBox* Scroll=Child<UScrollBox>(Page,TEXT("EndingStoryScroll"));
            Report.Check(Scroll&&Child<UTextBlock>(Page,TEXT("EndingStory"))->GetParent()==Scroll,TEXT("Entire authored ending body is in an editable scrolling widget"));
            if(Preset==EShopEndingTest::TruthChoice)
            {
                if(!Ending.At(EGamePhase::EndingChoice,10,TEXT("Truth-qualified state asks before resolving")))continue;
                Report.Check(Ending.Snapshot().Ending==EShopEnding::None&&Ending.Snapshot().Money==1500,TEXT("Choice does not auto-resolve or charge"));
                if(!Ending.Click(Page,TEXT("BtnReturnTruth"))||!Ending.VerifyEnding(EShopEnding::Returned,TEXT("Actual Return Truth button")))continue;
                Report.Check(Ending.Snapshot().RedemptionPaid==1500&&Ending.Snapshot().Money==0,TEXT("Return choice pays once"));
                if(Scroll)
                {
                    Scroll->SetScrollOffset(10000.f);
                    Report.Check(ShopUIPreview::Save(Ending.Root.Get(),TEXT("ending_Returned_story_bottom")),TEXT("Read the final paragraph of the true ending"));
                }
                if(!Report.Check(Ending.Controller->ProcessConsoleExec(*TestCommand,*GLog,Ending.Controller),TEXT("Editor test command also works after an ending")))continue;
                if(!Ending.Click(Page,TEXT("BtnLeaveCity"))||!Ending.VerifyEnding(EShopEnding::Redeemed,TEXT("Actual Leave City button declines true ending")))continue;
            }
            else
            {
                const EShopEnding Expected=Preset==EShopEndingTest::EmptyShelf?EShopEnding::FailedRedemption:Preset==EShopEndingTest::Pollution?EShopEnding::PollutionReleased:
                    Preset==EShopEndingTest::Closed?EShopEnding::Closed:EShopEnding::Redeemed;
                if(!Ending.VerifyEnding(Expected,TEXT("Manuscript ending through actual root/page")))continue;
            }
            if(Scroll)
            {
                Scroll->SetScrollOffset(10000.f);
                Report.Check(ShopUIPreview::Save(Ending.Root.Get(),Ending.PreviewPrefix+TEXT("_story_bottom")),TEXT("Render bottom of scrollable ending story"));
            }
            if(Ending.Click(Page,TEXT("BtnRestart")))Ending.VerifyRestartState();
        }
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
        Report.Note(TEXT("POLLUTION RELEASED: labelled transient pollution-per-sale boundary; all actions use real WBP buttons and the exact saved five-row ending table."));
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
