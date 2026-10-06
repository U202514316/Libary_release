#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShopTypes.generated.h"

UENUM(BlueprintType)
enum class EBookType : uint8 { Novel, Poem, History, Secret };
UENUM(BlueprintType)
enum class EBookLayer : uint8 { Table, Inside };
UENUM(BlueprintType)
// Keep serialized values stable. InsideSell is a retired phase; trading only happens at the front shop.
enum class EGamePhase : uint8 { Boot, Day, Sell, DayEnd, DuskChoice, Restock, Inside, InsideSell, Calm, History, NightEnd, Market, End, NightShop, NightSell };
UENUM(BlueprintType)
enum class EShopActionResult : uint8 { Rejected, Opened, Sold, WrongBook, NoMatch, OutOfStock, Success, Cancelled, InvalidPhase, InvalidId, InsufficientMoney, InsufficientPsychic, AlreadyDone, Expired, Unavailable, InvalidConfig };
UENUM(BlueprintType)
enum class EPollutionStage : uint8 { Safe, Light, Medium, Heavy };
UENUM(BlueprintType)
enum class EDecreeQuality : uint8 { Bronze, Silver, Gold };
UENUM(BlueprintType)
enum class ECustomerKind : uint8 { Normal, Hurry, Secret, Polluted };
UENUM(BlueprintType)
enum class EHistoryChoice : uint8 { Witness };
UENUM(BlueprintType)
enum class EShopEnding : uint8 { None, Closed, PollutionReleased, Returned, Cycle };
UENUM(BlueprintType)
enum class ENightChoice : uint8 { None, Restock, Inside };
UENUM(BlueprintType)
enum class EShopEventTrigger : uint8 { OnRead, OnDayStart, OnNightEnd, Manual };
UENUM(BlueprintType)
enum class ERentTiming : uint8 { BeforeDusk, NightEnd };
UENUM(BlueprintType)
enum class EShopEffectType : uint8 { Money, Psychic, Pollution, Enlighten, AddClue, RemoveClue, DestroySecretBooks, AlterSecretBook, IncomeMultiplier, PsychicGainMultiplier, DecayMultiplier, NightlyMoney, NightlyPollution, CustomerCountDelta, BlockLightSpread, NextPollutionBonus, SpawnFakeCustomer, SkipNightDecay, UnlockSecretBook, PermanentBusinessPenalty, ReturnLostSecretBook, ResetPollutionThresholds, NightIncomeMultiplier };
UENUM(BlueprintType)
enum class EShopEndingCondition : uint8 { NegativeBalance, PollutionLimit, FinalThresholds, FinalFallback };

/** Text is presentation only. These typed effects are the executable contract. */
USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FShopEffect
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EShopEffectType Type = EShopEffectType::Pollution;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Amount = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Multiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName TargetId;
    // Passive effect: 0 uses its owner's lifetime; -1 means permanent; >0 is logical turns.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DurationTurns = 0;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FBookData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EBookType BookType = EBookType::Novel;
    // Book origin, not its current shelf. Secret books keep Inside after being listed at the front shop.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EBookLayer Layer = EBookLayer::Table;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Cost = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Price = 20;
    // InitialStock is owned inventory for both layers. Collection offers are separate.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 InitialStock = 0;
    // Legacy field retained for serialized prototype assets; no longer added to stock.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 InitialOwnedStock = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CollectOfferPerNight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SaleEnlightenChance = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SaleEnlightenYield = 0;
    // -1 uses the global rule. A deliberate zero overrides the global rule.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PsychicYield = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionYield = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 EnlightenYield = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SealLevel = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> CluePool;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FCustomerData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText NeedLine;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECustomerKind Kind = ECustomerKind::Normal;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SpawnWeight = 1.f;
    // -1 selects the corresponding patience value in RunRules.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatienceSeconds = -1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinPollution = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxPollution = 99;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FCustomerRuntime
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName TemplateId;
    UPROPERTY(BlueprintReadOnly) EBookType NeedType = EBookType::Novel;
    // Requested book origin. All customers are physically at the front shop.
    UPROPERTY(BlueprintReadOnly) EBookLayer NeedLayer = EBookLayer::Table;
    UPROPERTY(BlueprintReadOnly) ECustomerKind Kind = ECustomerKind::Normal;
    UPROPERTY(BlueprintReadOnly) bool bServed = false;
    UPROPERTY(BlueprintReadOnly) bool bPolluted = false;
    UPROPERTY(BlueprintReadOnly) bool bSecret = false;
    UPROPERTY(BlueprintReadOnly) bool bObserved = false;
    UPROPERTY(BlueprintReadOnly) bool bFake = false;
    UPROPERTY(BlueprintReadOnly) float FakePollutionElapsed = 0.f;
    UPROPERTY(BlueprintReadOnly) int32 FakePollutionApplied = 0;
    UPROPERTY(BlueprintReadOnly) float MaxPatience = 30.f;
    UPROPERTY(BlueprintReadOnly) float Patience = 30.f;
    UPROPERTY(BlueprintReadOnly) EShopActionResult Resolution = EShopActionResult::Unavailable;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FSecretBookCopy
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bRead = false;
    UPROPERTY(BlueprintReadOnly) bool bSealed = true;
    UPROPERTY(BlueprintReadOnly) bool bAltered = false;
    UPROPERTY(BlueprintReadOnly) bool bPolluted = false;
    // Physical listing state; moving a copy never changes its read/seal/pollution flags.
    UPROPERTY(BlueprintReadOnly) bool bListedForSale = false;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FBookRuntime
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Stock = 0;
    UPROPERTY(BlueprintReadOnly) int32 AvailableToCollect = 0;
    UPROPERTY(BlueprintReadOnly) int32 ReadCopies = 0;
    UPROPERTY(BlueprintReadOnly) bool bAltered = false;
    UPROPERTY(BlueprintReadOnly) TArray<FSecretBookCopy> SecretCopies;
    // Derived secret-copy counts. Stock remains the total owned count; ordinary books use Stock.
    UPROPERTY(BlueprintReadOnly) int32 ListedCopies = 0;
    UPROPERTY(BlueprintReadOnly) int32 StoredCopies = 0;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FRunRules : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartMoney = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxDays = 35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Rent = 25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ERentTiming RentTiming = ERentTiming::BeforeDusk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NegativeDaysToClose = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bImmediateBankruptcy = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CustomersMin = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CustomersMax = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WeekTwoCustomerBonus = 1;
    // Legacy serialized name: number of NIGHT customers at the FRONT shop, never inside-store visitors.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Night Table Customers")) int32 InsideCustomers = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DaysPerWeek = 7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<EBookType> RandomNeedPool = { EBookType::Novel, EBookType::Poem, EBookType::History };
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatienceNormal = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatienceHurry = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatienceSecret = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatiencePolluted = 20.f;
    // Sample policy: at Medium+, duration is multiplied by this value, not drain speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PollutedPatienceMultiplier = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAllowEarlyClose = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWrongBookConsumesCustomer = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bNoMatchConsumesCustomer = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatienceDropRatePolluted = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartPsychic = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PsychicMax = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartPollution = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartEnlighten = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RedeemTarget = 1500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bReturnRequiresRedeemTarget = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 EnlightenWin = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WinPollutionThreshold = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LightThreshold = 31;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MediumThreshold = 61;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HeavyThreshold = 86;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionLimit = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionDecay = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionOnRead = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionOnCollect = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionOnSell = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionOnHistory = 15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionOnPollutedCustomer = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ReadPsychicGain = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CollectPsychicCost = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAllowRepeatRead = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRefillSecretOffersEachNight = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ClueDropChance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DecreeCandidateCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DecreeCandidateCountLight = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DecreeCandidateCountMedium = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DecreeCandidateCountHeavy = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DecreeCooldownTurns = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 EmergencyPollutionCut = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 EmergencyMoneyCost = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LightSpreadPerNight = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 FakeCustomerPollutionPerSecond = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 FakeCustomerMaxPollution = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AlteredBookReadPollution = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ReturnedBookReadPollution = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ReturnedBookSellPollution = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ReturnedBookFallbackPollution = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PermanentRentPenalty = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PermanentCustomerPenalty = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxRentPenalty = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxCustomerPenalty = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LoopholeDelayTurns = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HeavyGraceTurns = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HeavyPenalty = 15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bGoldSatisfiesHeavyGrace = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bStageCrossTriggersLoophole = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAdvanceTurnOnStageRise = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnableHistory = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnableMarket = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRequireCoreContentCounts = false;
    // Sample content is intentionally incomplete. Enable only after the authored tables arrive.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRequireFinalContentCounts = false;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FDecreeData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDecreeQuality Quality = EDecreeQuality::Bronze;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText EffectText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PsychicCost = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionCut = 15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText CostText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> CostEffect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> Effects;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText LoopholeText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> LoopholeEffect;
    // 0 uses the global delay. Each explicit night settlement advances one turn.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LoopholeDelay = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CooldownTurns = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EPollutionStage MinStage = EPollutionStage::Light;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EPollutionStage MaxStage = EPollutionStage::Heavy;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bFallback = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Text;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FDecreeRuntime
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName Id;
    UPROPERTY(BlueprintReadOnly) int32 EnactedTurn = 0;
    UPROPERTY(BlueprintReadOnly) int32 LoopholeAtTurn = 0;
    UPROPERTY(BlueprintReadOnly) int32 CooldownUntilTurn = 0;
    UPROPERTY(BlueprintReadOnly) bool bActive = true;
    UPROPERTY(BlueprintReadOnly) bool bLoopholeTriggered = false;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FEventData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EShopEventTrigger Trigger = EShopEventTrigger::OnRead;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName RequiredBookId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinDay = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxDay = 35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> RequiredClues;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Chance = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOncePerRun = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Text;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText OptionA;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> ResultA;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FMarketItemData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Category;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Price = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> TabooCost;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> Effect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FShopEffect> SideEffect;
    // 0 means every market; otherwise one-based week index.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Week = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOnePerRun = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FOwlLine : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Category;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 GuideIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Text;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FEndingData : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EShopEnding Ending = EShopEnding::Cycle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EShopEndingCondition Condition = EShopEndingCondition::FinalFallback;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Priority = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NegativeDaysRequired = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PollutionThreshold = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinEnlighten = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxPollutionExclusive = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRequireMoney = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinMoney = 1500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Text;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FRunSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Day = 0;
    UPROPERTY(BlueprintReadOnly) int32 MaxDays = 0;
    UPROPERTY(BlueprintReadOnly) int32 Money = 0;
    UPROPERTY(BlueprintReadOnly) int32 Psychic = 0;
    UPROPERTY(BlueprintReadOnly) int32 PsychicMax = 100;
    UPROPERTY(BlueprintReadOnly) int32 Pollution = 0;
    UPROPERTY(BlueprintReadOnly) int32 Enlighten = 0;
    UPROPERTY(BlueprintReadOnly) EPollutionStage PollutionStage = EPollutionStage::Safe;
    UPROPERTY(BlueprintReadOnly) int32 TotalStock = 0;
    UPROPERTY(BlueprintReadOnly) int32 TodayIncome = 0;
    UPROPERTY(BlueprintReadOnly) int32 TodayExpense = 0;
    UPROPERTY(BlueprintReadOnly) int32 TotalSold = 0;
    UPROPERTY(BlueprintReadOnly) int32 Rent = 0;
    UPROPERTY(BlueprintReadOnly) int32 Turn = 0;
    UPROPERTY(BlueprintReadOnly) int32 NegativeDays = 0;
    UPROPERTY(BlueprintReadOnly) EGamePhase Phase = EGamePhase::Boot;
    UPROPERTY(BlueprintReadOnly) ENightChoice NightChoice = ENightChoice::None;
    UPROPERTY(BlueprintReadOnly) EShopEnding Ending = EShopEnding::None;
    UPROPERTY(BlueprintReadOnly) TArray<FDecreeRuntime> ActiveDecrees;
    UPROPERTY(BlueprintReadOnly) TArray<FName> Clues;
    UPROPERTY(BlueprintReadOnly) TArray<FName> DecreeCandidates;
    UPROPERTY(BlueprintReadOnly) FName PendingEventId;
    UPROPERTY(BlueprintReadOnly) FText EndMessage;
};

USTRUCT(BlueprintType)
struct LIBARY_RELEASE_API FShopCommandResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSucceeded = false;
    UPROPERTY(BlueprintReadOnly) EShopActionResult Code = EShopActionResult::Rejected;
    UPROPERTY(BlueprintReadOnly) FText Message;
    UPROPERTY(BlueprintReadOnly) FName SubjectId;
};

// Internal value types. UI receives snapshots, never a mutable reference to this state.
struct FShopModifier
{
    FName SourceId;
    EShopEffectType Type = EShopEffectType::IncomeMultiplier;
    int32 Amount = 0;
    float Multiplier = 1.f;
    int32 EndTurn = -1;
};

struct FShopCatalog
{
    FRunRules Rules;
    TMap<FName, FBookData> Books;
    TMap<FName, FCustomerData> Customers;
    TMap<FName, FDecreeData> Decrees;
    TMap<FName, FEventData> Events;
    TMap<FName, FMarketItemData> MarketItems;
    TMap<FName, FOwlLine> OwlLines;
    TMap<FName, FEndingData> Endings;
};

struct FShopRunState
{
    int32 Day = 0, Turn = 0, Money = 0, Psychic = 0, Pollution = 0, Enlighten = 0;
    int32 TodayIncome = 0, TodayExpense = 0, TotalSold = 0;
    int32 NegativeDays = 0, LastRentDay = 0, LastSettledDay = 0, LastMarketDay = 0;
    int32 ActiveCustomer = INDEX_NONE, OwlTalkCount = 0;
    int32 HeavyDueTurn = INDEX_NONE;
    int32 PendingPollutionBonus = 0, PendingFakeCustomers = 0, SkipDecayNights = 0;
    int32 PeakPollutionThisCommand = 0;
    int32 RentPenalty = 0, CustomerPenalty = 0;
    bool StageResetPending = false;
    bool bGoldDuringHeavyGrace = false;
    bool bPollutionLimitReached = false;
    bool bPendingCalm = false;
    bool bNightCustomersGenerated = false;
    EGamePhase Phase = EGamePhase::Boot;
    EGamePhase ResumePhase = EGamePhase::Boot;
    ENightChoice NightChoice = ENightChoice::None;
    EShopEnding Ending = EShopEnding::None;
    FName PendingEventId;
    TMap<FName, FBookRuntime> Inventory;
    TArray<FCustomerRuntime> Customers;
    TArray<FDecreeRuntime> Decrees;
    TArray<FShopModifier> Modifiers;
    TArray<FName> DecreeCandidates, Clues, PendingEvents, WitnessedEvents, PurchasedItems, MarketStock, MarketSold;
    TArray<FName> LostSecretBooks;
    FRandomStream Random;
    FRandomStream CosmeticRandom;
};
