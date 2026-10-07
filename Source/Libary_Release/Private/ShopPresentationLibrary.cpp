#include "ShopPresentationLibrary.h"
#include "ShopBlueprintLibrary.h"
#include "ShopRunSubsystem.h"
#include "ShopCustomers.h"
#include "ShopPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"

namespace ShopPresentation
{
    const UShopRunSubsystem* Resolve(const UObject* Context)
    {
        if (!IsValid(Context)) return nullptr;
        if (const UShopRunSubsystem* Run = Cast<UShopRunSubsystem>(Context)) return Run;
        return UShopBlueprintLibrary::GetShopService(Context);
    }

    int32 Current(const UShopRunSubsystem* Run, FCustomerRuntime& Customer)
    {
        if (!Run || !Run->IsCurrentCustomerPresent()) return INDEX_NONE;
        const TArray<FCustomerRuntime> Queue = Run->GetCustomers_Implementation();
        for (int32 Index = 0; Index < Queue.Num(); ++Index)
        {
            if (!Queue[Index].bServed)
            {
                Customer = Queue[Index];
                return Index;
            }
        }
        return INDEX_NONE;
    }

    bool BookInfo(const UShopRunSubsystem* Run, FName BookId, FBookData& Book)
    {
        int32 Stock = 0;
        return Run && Run->GetBookInfo_Implementation(BookId, Book, Stock);
    }

    bool Candidate(const UShopRunSubsystem* Run, int32 Index, FDecreeData& Decree)
    {
        if (!Run) return false;
        const TArray<FName> Ids = Run->GetSnapshot_Implementation().DecreeCandidates;
        return Ids.IsValidIndex(Index) && Run->GetDecreeInfo(Ids[Index], Decree);
    }

    const TCHAR* CustomerName(ECustomerKind Kind)
    {
        switch (Kind)
        {
        case ECustomerKind::Hurry: return TEXT("急躁顾客");
        case ECustomerKind::Secret: return TEXT("秘密顾客");
        case ECustomerKind::Polluted: return TEXT("污染顾客");
        default: return TEXT("普通顾客");
        }
    }

    const TCHAR* const TutorialLines[] =
    {
        TEXT("欢迎来到表里书店。我是夜枭，将带你熟悉这间书店。"),
        TEXT("白天在表书店接待顾客。每天的基础顾客为三人，处理完队伍后进入日间结算。"),
        TEXT("你会先在柜台等待，顾客到店后点击其画像，才会展开需求与接待选项。每两位顾客之间有二至四秒间隔，耐心从到店后开始计算。"),
        TEXT("小说、诗集和史书满足普通购书需求。秘密顾客只购买已经上架的秘密书。"),
        TEXT("在售卖界面选择有货且符合需求的书。出售成功后，资金与库存会自动更新。"),
        TEXT("遇到可疑顾客，可以先观察，再决定是否拒绝。观察不会暂停营业中的耐心计时。"),
        TEXT("白天结束后查看收入与支出，继续进入夜晚。选择前往商人处进货，或进入里书店。"),
        TEXT("商人提供普通书补货。里书店用于管理秘密书：上架供表店售卖，或把已上架的书撤回。"),
        TEXT("秘密书与异常事件可能增加污染。出现安抚面板时，检查律令的效果、代价和漏洞，再作选择。"),
        TEXT("结束夜晚后查看结算，继续下一天。正式营业将从教程结束后开始；祝你守好这间书店。")
    };
}

UUserWidget* UShopPresentationLibrary::GetRootView(const UObject* WorldContextObject)
{
    UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
    if (!World) return nullptr;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (const AShopPlayerController* Controller = Cast<AShopPlayerController>(It->Get()))
            if (Controller->IsLocalController()) return Controller->RootWidget.Get();
    }
    return nullptr;
}

FText UShopPresentationLibrary::GetStatText(const UObject* WorldContextObject, EShopStatField Field)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::FromString(TEXT("—"));
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    switch (Field)
    {
    case EShopStatField::Day:
        return FText::FromString(FString::Printf(TEXT("第 %d / %d 天"), Snapshot.Day, Snapshot.MaxDays));
    case EShopStatField::Money:
        return FText::FromString(FString::Printf(TEXT("资金 %d"), Snapshot.Money));
    case EShopStatField::Psychic:
        return FText::FromString(FString::Printf(TEXT("灵能 %d"), Snapshot.Psychic));
    case EShopStatField::Pollution:
        return FText::FromString(FString::Printf(TEXT("污染 %d  ·  启蒙 %d"), Snapshot.Pollution, Snapshot.Enlighten));
    case EShopStatField::Income:
        return FText::FromString(FString::Printf(TEXT("今日收入 %d"), Snapshot.TodayIncome));
    case EShopStatField::Expense:
        return FText::FromString(FString::Printf(TEXT("今日支出 %d"), Snapshot.TodayExpense));
    case EShopStatField::Enlighten:
        return FText::FromString(FString::Printf(TEXT("启蒙 %d"), Snapshot.Enlighten));
    case EShopStatField::Stock:
    {
        int64 Total = 0;
        for (FName Id : Run->GetBookIds())
        {
            FBookData Book;
            int32 Stock = 0;
            if (Run->GetBookInfo_Implementation(Id, Book, Stock)) Total += Stock;
        }
        return FText::FromString(FString::Printf(TEXT("库存 %lld"), Total));
    }
    case EShopStatField::Queue:
    {
        int32 Count = 0;
        for (const FCustomerRuntime& Customer : Run->GetCustomers_Implementation())
            if (!Customer.bServed) ++Count;
        return FText::FromString(FString::Printf(TEXT("待接待 %d 人"), Count));
    }
    default: return FText::GetEmpty();
    }
}

FText UShopPresentationLibrary::GetHudStatText(const UObject* WorldContextObject, EShopStatField Field)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::FromString(TEXT("—"));
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    if (Field == EShopStatField::Psychic)
        return FText::AsNumber(Snapshot.Psychic, &FNumberFormattingOptions::DefaultNoGrouping());
    if (Field == EShopStatField::Pollution)
        return FText::FromString(FString::Printf(TEXT("污染 %d"), Snapshot.Pollution));
    return GetStatText(WorldContextObject, Field);
}

int32 UShopPresentationLibrary::GetCurrentCustomerIndex(const UObject* WorldContextObject)
{
    FCustomerRuntime Customer;
    return ShopPresentation::Current(ShopPresentation::Resolve(WorldContextObject), Customer);
}

FText UShopPresentationLibrary::GetCustomerName(const UObject* WorldContextObject)
{
    FCustomerRuntime Customer;
    if (ShopPresentation::Current(ShopPresentation::Resolve(WorldContextObject), Customer) == INDEX_NONE)
        return FText::FromString(TEXT("暂无顾客"));
    if (Customer.bObserved && Customer.bFake) return FText::FromString(TEXT("伪装顾客"));
    return FText::FromString(ShopPresentation::CustomerName(Customer.Kind));
}

FText UShopPresentationLibrary::GetCustomerNeedText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FCustomerRuntime Customer;
    const int32 Index = ShopPresentation::Current(Run, Customer);
    return Index != INDEX_NONE ? Run->BuildCustomerNeedText(Index) : FText::FromString(TEXT("当前没有待接待顾客。"));
}

FText UShopPresentationLibrary::GetCustomerPatienceText(const UObject* WorldContextObject)
{
    FCustomerRuntime Customer;
    if (ShopPresentation::Current(ShopPresentation::Resolve(WorldContextObject), Customer) == INDEX_NONE)
        return FText::GetEmpty();
    return FText::FromString(FString::Printf(TEXT("耐心 %d / %d 秒"),
        FMath::CeilToInt(FMath::Max(0.f, Customer.Patience)), FMath::CeilToInt(FMath::Max(0.f, Customer.MaxPatience))));
}

ECustomerKind UShopPresentationLibrary::GetCustomerKind(const UObject* WorldContextObject)
{
    FCustomerRuntime Customer;
    ShopPresentation::Current(ShopPresentation::Resolve(WorldContextObject), Customer);
    return Customer.Kind;
}

int32 UShopPresentationLibrary::GetCustomerPortraitSlot(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FCustomerRuntime Customer;
    const int32 Index = ShopPresentation::Current(Run, Customer);
    if (Index == INDEX_NONE) return INDEX_NONE;
    if (Customer.PortraitSlot != INDEX_NONE) return Customer.PortraitSlot;
    // Compatibility for manually constructed queues that predate the persisted portrait identity.
    return ShopCustomers::ChoosePortrait(Customer.Kind, Run->GetSnapshot_Implementation().Day,
        Index, Customer.TemplateId, TArray<FCustomerRuntime>());
}

ESlateVisibility UShopPresentationLibrary::GetCustomerVisibility(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    return Run && Run->IsCurrentCustomerPresent() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
}

ESlateVisibility UShopPresentationLibrary::GetInteractionVisibility(const UObject* WorldContextObject, bool bCustomerSelected)
{
    return bCustomerSelected ? GetCustomerVisibility(WorldContextObject) : ESlateVisibility::Collapsed;
}

FText UShopPresentationLibrary::GetCounterStatusText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    return FText::FromString(Run->IsCurrentCustomerPresent()
        ? TEXT("顾客已到店 · 点击右侧顾客开始接待")
        : TEXT("店主正在收银台等候 · 下一位顾客即将到店"));
}

FText UShopPresentationLibrary::GetBookTitle(const UObject* WorldContextObject, FName BookId)
{
    FBookData Book;
    return ShopPresentation::BookInfo(ShopPresentation::Resolve(WorldContextObject), BookId, Book)
        ? Book.DisplayName : FText::FromString(TEXT("未知书籍"));
}

FText UShopPresentationLibrary::GetBookDescription(const UObject* WorldContextObject, FName BookId)
{
    FBookData Book;
    if (!ShopPresentation::BookInfo(ShopPresentation::Resolve(WorldContextObject), BookId, Book)) return FText::GetEmpty();
    FString Description = FString::Printf(TEXT("%s  ·  基础售价 %d"), *UShopBlueprintLibrary::GetBookTypeText(Book.BookType).ToString(), Book.Price);
    if (Book.BookType == EBookType::Secret)
        Description += TEXT("\n上架后供表店秘密顾客购买。");
    else Description += FString::Printf(TEXT("  ·  进货价 %d"), Book.Cost);
    if (Book.SaleEnlightenYield > 0)
        Description += Book.SaleEnlightenChance >= 1.f ? FString::Printf(TEXT("\n成功售出：启蒙 +%d"), Book.SaleEnlightenYield) : FString::Printf(TEXT("\n售出：%d%% 概率启蒙 +%d"),
            FMath::RoundToInt(Book.SaleEnlightenChance * 100.f), Book.SaleEnlightenYield);
    return FText::FromString(Description);
}

FText UShopPresentationLibrary::GetBookStockText(const UObject* WorldContextObject, FName BookId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FBookData Book;
    FBookRuntime Runtime;
    if (!ShopPresentation::BookInfo(Run, BookId, Book) || !Run->GetBookRuntime(BookId, Runtime))
        return FText::FromString(TEXT("库存尚未初始化"));
    return FText::FromString(Book.BookType == EBookType::Secret
        ? FString::Printf(TEXT("拥有 %d  ·  里店 %d  ·  已上架 %d"), Runtime.Stock, Runtime.StoredCopies, Runtime.ListedCopies)
        : FString::Printf(TEXT("库存 %d 本"), Runtime.Stock));
}

bool UShopPresentationLibrary::CanBookAction(const UObject* WorldContextObject, FName BookId, EShopBookAction Action)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FBookData Book;
    FBookRuntime Runtime;
    if (!ShopPresentation::BookInfo(Run, BookId, Book) || !Run->GetBookRuntime(BookId, Runtime)) return false;
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    const bool bSecret = Book.Layer == EBookLayer::Inside && Book.BookType == EBookType::Secret;
    switch (Action)
    {
    case EShopBookAction::Buy:
        return Snapshot.Phase == EGamePhase::Restock && Book.Layer == EBookLayer::Table && Book.BookType != EBookType::Secret &&
            Book.Cost > 0 && Snapshot.Money >= Book.Cost && Runtime.Stock < MAX_int32;
    case EShopBookAction::List:
        return Snapshot.Phase == EGamePhase::Inside && bSecret && Runtime.StoredCopies > 0;
    case EShopBookAction::Unlist:
        return Snapshot.Phase == EGamePhase::Inside && bSecret && Runtime.ListedCopies > 0;
    case EShopBookAction::Read:
    {
        FText Reason; return Run->CanReadSecret(BookId, Reason);
    }
    case EShopBookAction::BuySecret:
    {
        FText Reason; return Run->CanBuyMarketItem(BookId, Reason);
    }
    case EShopBookAction::Sell:
    {
        if (Snapshot.Phase != EGamePhase::Sell && Snapshot.Phase != EGamePhase::NightSell) return false;
        const TArray<FCustomerRuntime> Customers = Run->GetCustomers_Implementation();
        const int32 Active = Run->GetActiveCustomerIndex();
        if (!Customers.IsValidIndex(Active)) return false;
        const FCustomerRuntime& Customer = Customers[Active];
        return !Customer.bServed && !Customer.bFake && Customer.Patience > 0.f &&
            (bSecret ? Runtime.ListedCopies > 0 : Runtime.Stock > 0);
    }
    default: return false;
    }
}

FName UShopPresentationLibrary::GetCandidateId(const UObject* WorldContextObject, int32 Index)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return NAME_None;
    const TArray<FName> Candidates = Run->GetSnapshot_Implementation().DecreeCandidates;
    return Candidates.IsValidIndex(Index) ? Candidates[Index] : NAME_None;
}

FText UShopPresentationLibrary::GetDecreeTitle(const UObject* WorldContextObject, int32 Index)
{
    FDecreeData Decree;
    return ShopPresentation::Candidate(ShopPresentation::Resolve(WorldContextObject), Index, Decree)
        ? Decree.DisplayName : FText::GetEmpty();
}

FText UShopPresentationLibrary::GetDecreeText(const UObject* WorldContextObject, int32 Index)
{
    FDecreeData Decree;
    if (!ShopPresentation::Candidate(ShopPresentation::Resolve(WorldContextObject), Index, Decree)) return FText::GetEmpty();
    FString Description = !Decree.EffectText.IsEmpty() ? Decree.EffectText.ToString() : Decree.Text.ToString();
    if (Description.IsEmpty()) Description = FString::Printf(TEXT("降低 %d 点污染。"), Decree.PollutionCut);
    if (!Decree.LoopholeText.IsEmpty()) Description += TEXT("\n漏洞：") + Decree.LoopholeText.ToString();
    const FRunRules Rules = ShopPresentation::Resolve(WorldContextObject)->GetRunRules();
    if (!Decree.bFallback) Description += FString::Printf(TEXT("\n持续 %d 回合 · 到期后冷却 %d 回合"),
        Decree.LoopholeDelay > 0 ? Decree.LoopholeDelay : Rules.LoopholeDelayTurns,
        Decree.CooldownTurns > 0 ? Decree.CooldownTurns : Rules.DecreeCooldownTurns);
    return FText::FromString(Description);
}

FText UShopPresentationLibrary::GetDecreeCostText(const UObject* WorldContextObject, int32 Index)
{
    FDecreeData Decree;
    if (!ShopPresentation::Candidate(ShopPresentation::Resolve(WorldContextObject), Index, Decree)) return FText::GetEmpty();
    FString Cost = FString::Printf(TEXT("消耗灵能 %d  ·  污染降低 %d"), Decree.PsychicCost, Decree.PollutionCut);
    if (!Decree.CostText.IsEmpty()) Cost += TEXT("\n代价：") + Decree.CostText.ToString();
    return FText::FromString(Cost);
}

bool UShopPresentationLibrary::CanChooseDecree(const UObject* WorldContextObject, int32 Index)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    const FName Id = GetCandidateId(WorldContextObject, Index);
    FText Reason;
    return Run && !Id.IsNone() && Run->CanEnactDecree(Id, Reason);
}

ESlateVisibility UShopPresentationLibrary::GetDecreeEntryVisibility(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return ESlateVisibility::Collapsed;
    const EGamePhase Phase = Run->GetSnapshot_Implementation().Phase;
    return Phase == EGamePhase::Day || Phase == EGamePhase::Sell || Phase == EGamePhase::Inside
        ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}

FText UShopPresentationLibrary::GetDecreeNameById(const UObject* WorldContextObject, FName DecreeId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FDecreeData Data;
    if (!Run || !Run->GetDecreeInfo(DecreeId, Data)) return FText::GetEmpty();
    const FString Prefix = DecreeId.ToString().StartsWith(TEXT("gold_")) ? TEXT("金 · ")
        : DecreeId.ToString().StartsWith(TEXT("silver_")) ? TEXT("银 · ")
        : DecreeId.ToString().StartsWith(TEXT("bronze_")) ? TEXT("铜 · ") : TEXT("");
    return FText::FromString(Prefix + Data.DisplayName.ToString());
}

FText UShopPresentationLibrary::GetDecreeDescriptionById(const UObject* WorldContextObject, FName DecreeId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FDecreeData Data;
    if (!Run || !Run->GetDecreeInfo(DecreeId, Data)) return FText::GetEmpty();
    FString Description = TEXT("律令效果\n") + (!Data.EffectText.IsEmpty() ? Data.EffectText.ToString() : Data.Text.ToString());
    Description += FString::Printf(TEXT("\n\n释放消耗\n灵能 %d 点；污染降低 %d 点。"), Data.PsychicCost, Data.PollutionCut);
    if (!Data.CostText.IsEmpty()) Description += TEXT("\n") + Data.CostText.ToString();
    if (!Data.LoopholeText.IsEmpty()) Description += TEXT("\n\n到期反噬\n") + Data.LoopholeText.ToString();
    if (!Data.bFallback)
    {
        const FRunRules Rules = Run->GetRunRules();
        Description += FString::Printf(TEXT("\n\n持续与冷却\n持续 %d 回合，到期后冷却 %d 回合。\n夜间结算推进一回合%s。永久代价不会随律令到期消失。"),
            Data.LoopholeDelay > 0 ? Data.LoopholeDelay : Rules.LoopholeDelayTurns,
            Data.CooldownTurns > 0 ? Data.CooldownTurns : Rules.DecreeCooldownTurns,
            Rules.bAdvanceTurnOnStageRise ? TEXT("，跨入更高污染阶段也会推进") : TEXT(""));
    }
    Description += TEXT("\n\n当前状态\n") + GetDecreeAvailabilityText(WorldContextObject, DecreeId).ToString();
    return FText::FromString(Description);
}

FText UShopPresentationLibrary::GetDecreeSummaryById(const UObject* WorldContextObject, FName DecreeId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    FDecreeData Data;
    return Run && Run->GetDecreeInfo(DecreeId, Data)
        ? FText::FromString(FString::Printf(TEXT("灵能消耗 %d  ·  污染降低 %d"), Data.PsychicCost, Data.PollutionCut)) : FText::GetEmpty();
}

bool UShopPresentationLibrary::HasDecreeSelection(const UObject* WorldContextObject, FName DecreeId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FDecreeData Data;
    return Run && !DecreeId.IsNone() && Run->GetDecreeInfo(DecreeId, Data) && Data.bEnabled;
}

bool UShopPresentationLibrary::CanConfirmDecree(const UObject* WorldContextObject, FName DecreeId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FText Reason;
    return Run && !DecreeId.IsNone() && Run->CanEnactDecree(DecreeId, Reason);
}

FText UShopPresentationLibrary::GetDecreeAvailabilityText(const UObject* WorldContextObject, FName DecreeId, bool bCompact)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FDecreeData Data;
    if (!Run || DecreeId.IsNone()) return FText::FromString(bCompact ? TEXT("") : TEXT("先从左侧选择一条律令"));
    if (!Run->GetDecreeInfo(DecreeId, Data) || !Data.bEnabled) return FText::FromString(TEXT("暂不可用"));
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    const FDecreeRuntime* Latest = nullptr;
    for (const FDecreeRuntime& Runtime : Snapshot.ActiveDecrees)
        if (Runtime.Id == DecreeId && (!Latest || Runtime.EnactedTurn >= Latest->EnactedTurn)) Latest = &Runtime;
    if (Latest && Latest->bActive) return FText::FromString(bCompact ? TEXT("生效中") : FString::Printf(TEXT("生效中 · 剩余 %d 回合"), FMath::Max(0, Latest->LoopholeAtTurn - Snapshot.Turn)));
    if (Latest && Latest->CooldownUntilTurn > Snapshot.Turn) return FText::FromString(bCompact
        ? FString::Printf(TEXT("冷却 %d 回合"), Latest->CooldownUntilTurn - Snapshot.Turn)
        : FString::Printf(TEXT("冷却中 · 剩余 %d 回合"), Latest->CooldownUntilTurn - Snapshot.Turn));
    if (!Data.bFallback && (Snapshot.PollutionStage < Data.MinStage || Snapshot.PollutionStage > Data.MaxStage))
        return FText::FromString(bCompact ? TEXT("尚未解锁") : TEXT("当前污染阶段未解锁；可查看介绍"));
    if (Snapshot.Psychic < Data.PsychicCost) return FText::FromString(TEXT("灵能不足"));
    if (CanConfirmDecree(WorldContextObject, DecreeId)) return FText::FromString(bCompact ? TEXT("可释放") : TEXT("点击确定释放 · 消耗与代价见介绍"));
    return FText::FromString(bCompact ? TEXT("本次不可用") : TEXT("未进入本次候选，或不满足释放代价"));
}

FLinearColor UShopPresentationLibrary::GetDecreeSelectionColor(FName DecreeId, FName SelectedDecreeId)
{
    return !SelectedDecreeId.IsNone() && DecreeId == SelectedDecreeId
        ? FLinearColor(1.f, 0.018f, 0.008f, 1.f) : FLinearColor::Transparent;
}

FLinearColor UShopPresentationLibrary::GetDecreeCardTint(const UObject* WorldContextObject, FName DecreeId)
{
    return CanConfirmDecree(WorldContextObject, DecreeId) ? FLinearColor::White : FLinearColor(0.42f, 0.42f, 0.42f, 1.f);
}

ESlateVisibility UShopPresentationLibrary::GetUnselectedDecreeVisibility(FName SelectedDecreeId)
{
    return SelectedDecreeId.IsNone() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
}

ESlateVisibility UShopPresentationLibrary::GetEmergencyDecreeVisibility(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    return Run && Run->GetSnapshot_Implementation().DecreeCandidates.Contains(TEXT("emergency_calm"))
        ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}

FText UShopPresentationLibrary::GetDecreeStatusText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    FString Text = FString::Printf(TEXT("当前回合 %d。每次夜结推进一回合；跨入更高污染阶段也会推进。永久代价不会随律令到期消失。\n"), Snapshot.Turn);
    for (FName Id : Run->GetDecreeIds())
    {
        FDecreeData Data; if (!Run->GetDecreeInfo(Id, Data) || !Data.bEnabled) continue;
        const FDecreeRuntime* Latest = nullptr;
        for (const FDecreeRuntime& Runtime : Snapshot.ActiveDecrees)
            if (Runtime.Id == Id && (!Latest || Runtime.EnactedTurn >= Latest->EnactedTurn)) Latest = &Runtime;
        FString Status;
        if (Latest && Latest->bActive) Status = FString::Printf(TEXT("生效中，到反噬剩余 %d 回合"), FMath::Max(0, Latest->LoopholeAtTurn - Snapshot.Turn));
        else if (Latest && Latest->CooldownUntilTurn > Snapshot.Turn) Status = FString::Printf(TEXT("冷却中，剩余 %d 回合"), Latest->CooldownUntilTurn - Snapshot.Turn);
        else if (Snapshot.PollutionStage < Data.MinStage || Snapshot.PollutionStage > Data.MaxStage) Status = TEXT("当前污染阶段未解锁");
        else { FText Reason; Status = Run->CanEnactDecree(Id, Reason) ? TEXT("已就绪") : TEXT("代价不足或当前不可用"); }
        Text += Data.DisplayName.ToString() + TEXT("：") + Status + TEXT("\n");
    }
    return FText::FromString(Text);
}

FText UShopPresentationLibrary::GetBacklashLogText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const TArray<FText> Log = Run->GetSnapshot_Implementation().DecreeBacklashLog;
    FString Text = TEXT("反噬记录（最近发生的在前）\n");
    if (Log.IsEmpty()) Text += TEXT("本局尚未发生律令反噬。");
    for (int32 Index = Log.Num()-1; Index >= 0; --Index) Text += Log[Index].ToString() + TEXT("\n\n");
    return FText::FromString(Text);
}

FText UShopPresentationLibrary::GetLatestBacklashText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    return Snapshot.Phase != EGamePhase::Boot && !Snapshot.DecreeBacklashLog.IsEmpty()
        ? FText::Format(FText::FromString(TEXT("最近反噬：{0}")), Snapshot.DecreeBacklashLog.Last()) : FText::GetEmpty();
}

FText UShopPresentationLibrary::GetHistoryTitle(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FEventData Event;
    return Run && Run->GetEventInfo(Run->GetSnapshot_Implementation().PendingEventId, Event) ? Event.Title : FText::GetEmpty();
}
FText UShopPresentationLibrary::GetHistoryBody(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FEventData Event;
    return Run && Run->GetEventInfo(Run->GetSnapshot_Implementation().PendingEventId, Event) ? Event.Text : FText::GetEmpty();
}
FText UShopPresentationLibrary::GetHistoryProgress(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    return Run ? FText::FromString(FString::Printf(TEXT("本局已收集 %d / 7 张 · 获得新残页：启蒙 +%d。奖励已结算，关闭不会重复发放。"),
        Run->GetSnapshot_Implementation().CollectedHistoryPages.Num(), Run->GetRunRules().HistoryFragmentEnlightenGain)) : FText::GetEmpty();
}
bool UShopPresentationLibrary::CanVisitBlackMarket(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); return Run && Run->CanOpenMarket();
}
ESlateVisibility UShopPresentationLibrary::GetBlackMarketVisibility(const UObject* WorldContextObject)
{
    return CanVisitBlackMarket(WorldContextObject) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}
FText UShopPresentationLibrary::GetMarketBookDescription(const UObject* WorldContextObject, FName BookId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FMarketItemData Item;
    if (!Run || !Run->GetMarketItemInfo(BookId, Item)) return FText::GetEmpty();
    return FText::FromString(FString::Printf(TEXT("黑市价 %d · 基础污染 +%d\n限购 1 册，入里店后上架。"), Item.Price, Run->GetRunRules().MarketBookPollution));
}
FText UShopPresentationLibrary::GetReadButtonText(const UObject* WorldContextObject, FName BookId)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject); FBookRuntime Book;
    if (!Run || !Run->GetBookRuntime(BookId, Book)) return FText::FromString(TEXT("翻阅"));
    const FRunRules Rules = Run->GetRunRules();
    return FText::FromString(Book.LastReadDay == Run->GetSnapshot_Implementation().Day ? FString(TEXT("今晚已翻阅"))
        : FString::Printf(TEXT("翻阅 · 污染 +%d / 灵能 +%d"), Rules.PollutionOnRead, Rules.ReadPsychicGain));
}

FText UShopPresentationLibrary::GetEndingTitle(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    if (Snapshot.Phase == EGamePhase::EndingChoice) return FText::FromString(TEXT("是否将真相归还给众人？"));
    FEndingData Ending;
    if (Run->GetEndingInfo(Snapshot.Ending, Ending)) return Ending.Title;
    switch (Snapshot.Ending)
    {
    case EShopEnding::Closed: return FText::FromString(TEXT("关门"));
    case EShopEnding::PollutionReleased: return FText::FromString(TEXT("污染释放"));
    case EShopEnding::Returned: return FText::FromString(TEXT("归还"));
    case EShopEnding::Cycle: return FText::FromString(TEXT("守旧循环"));
    case EShopEnding::FailedRedemption: return FText::FromString(TEXT("空书架"));
    case EShopEnding::Redeemed: return FText::FromString(TEXT("赎身离场"));
    default: return FText::GetEmpty();
    }
}

FText UShopPresentationLibrary::GetEndingText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    if (Snapshot.Phase == EGamePhase::EndingChoice)
        return FText::FromString(TEXT("你已凑够赎身金，也获得了足够的启蒙，并控制住污染。\n\n你可以留下来经营书店，将埋藏的真相归还给众人；也可以赎回自由，独自离开这座城市。\n\n请选择你的决定。确认后支付赎身金，并进入对应结局。"));
    return Snapshot.EndMessage;
}

ESlateVisibility UShopPresentationLibrary::GetEndingChoiceVisibility(const UObject* WorldContextObject, bool bChoice)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return ESlateVisibility::Collapsed;
    const EGamePhase Phase = Run->GetSnapshot_Implementation().Phase;
    return (bChoice ? Phase == EGamePhase::EndingChoice : Phase == EGamePhase::End) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}

FText UShopPresentationLibrary::GetEndingConditionText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    if (Snapshot.Phase == EGamePhase::EndingChoice)
    {
        FEndingData Returned; Run->GetEndingInfo(EShopEnding::Returned, Returned);
        return FText::FromString(FString::Printf(TEXT("第 %d / %d 天 · 当前资金 %d · 赎身金 %d\n启蒙 %d · 污染 %d · 等待你的选择"),
            Snapshot.Day, Snapshot.MaxDays, Snapshot.Money, Returned.RedemptionCost, Snapshot.Enlighten, Snapshot.Pollution));
    }
    FEndingData Data;
    if (!Run->GetEndingInfo(Snapshot.Ending, Data)) return FText::GetEmpty();
    FString Reason;
    switch (Data.Condition)
    {
    case EShopEndingCondition::NegativeBalance:
        Reason = FString::Printf(TEXT("连续 %d 天结算后资金为负，经营提前结束。"), Data.NegativeDaysRequired); break;
    case EShopEndingCondition::PollutionLimit:
        Reason = FString::Printf(TEXT("污染曾达到 %d，书店失去控制，经营提前结束。"), Data.PollutionThreshold); break;
    case EShopEndingCondition::FinalThresholds:
        Reason = FString::Printf(TEXT("完成 %d 天：启蒙达到 %d，污染低于 %d。"), Snapshot.MaxDays, Data.MinEnlighten, Data.MaxPollutionExclusive);
        if (Data.bRequireMoney) Reason += FString::Printf(TEXT("资金至少 %d。"), Data.MinMoney);
        break;
    case EShopEndingCondition::FinalFallback:
        Reason = FString::Printf(TEXT("完成 %d 天，但未同时满足“归还”的条件，进入新的循环。"), Snapshot.MaxDays); break;
    case EShopEndingCondition::FinalMoneyBelow:
        Reason = FString::Printf(TEXT("完成 %d 天，资金不足 %d，未能赎回自由。"), Snapshot.MaxDays, Data.MinMoney); break;
    case EShopEndingCondition::FinalMoneyAtLeast:
        Reason = FString::Printf(TEXT("完成 %d 天，已凑够赎身金，独自离开城市。"), Snapshot.MaxDays); break;
    }
    if (Snapshot.RedemptionPaid > 0) Reason += FString::Printf(TEXT("判定前资金 %d；已支付赎身金 %d。"), Snapshot.Money + Snapshot.RedemptionPaid, Snapshot.RedemptionPaid);
    return FText::FromString(Reason + FString::Printf(TEXT("\n最终记录：第 %d 天  ·  资金 %d  ·  污染 %d  ·  启蒙 %d"), Snapshot.Day, Snapshot.Money, Snapshot.Pollution, Snapshot.Enlighten));
}

FLinearColor UShopPresentationLibrary::GetEndingAccent(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FLinearColor::White;
    switch (Run->GetSnapshot_Implementation().Ending)
    {
    case EShopEnding::Closed: return FLinearColor(.28f,.19f,.12f,1.f);
    case EShopEnding::PollutionReleased: return FLinearColor(.5f,.035f,.045f,1.f);
    case EShopEnding::Returned: return FLinearColor(.1f,.35f,.25f,1.f);
    case EShopEnding::Cycle: return FLinearColor(.23f,.18f,.37f,1.f);
    case EShopEnding::FailedRedemption: return FLinearColor(.34f,.22f,.12f,1.f);
    case EShopEnding::Redeemed: return FLinearColor(.62f,.43f,.17f,1.f);
    default: return FLinearColor::White;
    }
}

int32 UShopPresentationLibrary::GetPhasePageIndex(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return 0;
    switch (Run->GetSnapshot_Implementation().Phase)
    {
    case EGamePhase::Day: case EGamePhase::NightShop: return 2;
    case EGamePhase::Sell: case EGamePhase::NightSell: return 3;
    case EGamePhase::DayEnd: return 4;
    case EGamePhase::DuskChoice: return 5;
    case EGamePhase::Restock: return 6;
    case EGamePhase::Inside: case EGamePhase::InsideSell: return 7;
    case EGamePhase::Calm: return 8;
    case EGamePhase::NightEnd: return 9;
    case EGamePhase::End: case EGamePhase::EndingChoice: return 10;
    case EGamePhase::History: return 11;
    case EGamePhase::Market: return 12;
    default: return 0;
    }
}

FText UShopPresentationLibrary::FormatActionResult(EShopActionResult Result)
{
    const TCHAR* Message = TEXT("操作未完成，请查看当前阶段和资源。");
    switch (Result)
    {
    case EShopActionResult::Success: Message = TEXT("操作成功。"); break;
    case EShopActionResult::Opened: Message = TEXT("请选择符合顾客需求的书籍。"); break;
    case EShopActionResult::Sold: Message = TEXT("出售成功，收入与库存已更新。"); break;
    case EShopActionResult::WrongBook: Message = TEXT("这本书不符合顾客需求。"); break;
    case EShopActionResult::NoMatch: Message = TEXT("没有符合需求的可售库存。"); break;
    case EShopActionResult::OutOfStock: Message = TEXT("没有可售库存，请检查库存或秘密书上架数量。"); break;
    case EShopActionResult::Cancelled: Message = TEXT("已取消选书。"); break;
    case EShopActionResult::InvalidPhase: Message = TEXT("当前阶段不能执行此操作。"); break;
    case EShopActionResult::InvalidId: Message = TEXT("目标已失效，请重新选择。"); break;
    case EShopActionResult::InsufficientMoney: Message = TEXT("资金不足。"); break;
    case EShopActionResult::InsufficientPsychic: Message = TEXT("灵能不足。"); break;
    case EShopActionResult::AlreadyDone: Message = TEXT("此操作已经完成。"); break;
    case EShopActionResult::Expired: Message = TEXT("顾客耐心耗尽，已经离开。"); break;
    case EShopActionResult::Unavailable: Message = TEXT("当前条件下无法执行此操作。"); break;
    case EShopActionResult::InvalidConfig: Message = TEXT("数据配置无效，请检查经营数据表。"); break;
    case EShopActionResult::Rejected: break;
    }
    return FText::FromString(Message);
}

FSlateBrush UShopPresentationLibrary::MakeBookCoverBrush(UTexture2D* Texture)
{
    FSlateBrush Brush;
    Brush.SetResourceObject(Texture);
    Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
    if (Texture) Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    return Brush;
}

FText UShopPresentationLibrary::FormatCustomerResult(EShopActionResult Result)
{
    return Result == EShopActionResult::Rejected ? FText::FromString(TEXT("已谢绝这位顾客。")) : FormatActionResult(Result);
}

FText UShopPresentationLibrary::FormatCommandResult(const FShopCommandResult& Result)
{
    return !Result.Message.IsEmpty() ? Result.Message : FormatActionResult(Result.Code);
}

FText UShopPresentationLibrary::FormatObservation(const FCustomerRuntime& Customer)
{
    if (Customer.bFake) return FText::FromString(TEXT("观察结果：这位顾客是伪装者，不会购买图书。请及时谢绝。"));
    if (Customer.bPolluted) return FText::FromString(TEXT("观察结果：顾客受到污染，需要普通书。可以按需求交易，也可以谢绝。"));
    if (Customer.bSecret) return FText::FromString(TEXT("观察结果：秘密顾客，需要已经上架的秘密书。"));
    return FText::FromString(TEXT("观察结果：没有发现异常，可根据需求正常售书。"));
}

FText UShopPresentationLibrary::GetTutorialText(int32 Index)
{
    return Index >= 0 && Index < GetTutorialCount() ? FText::FromString(ShopPresentation::TutorialLines[Index]) : FText::GetEmpty();
}

int32 UShopPresentationLibrary::GetTutorialCount()
{
    return UE_ARRAY_COUNT(ShopPresentation::TutorialLines);
}

AShopPlayerController* UShopPresentationLibrary::GetShopController(const UObject* Context)
{
    UUserWidget* Root = GetRootView(Context);
    return Root ? Cast<AShopPlayerController>(Root->GetOwningPlayer()) : nullptr;
}

FText UShopPresentationLibrary::GetGameGuideTitle(const UObject* Context, int32 Section)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(Context);
    const EShopEnding Endings[] = { EShopEnding::PollutionReleased, EShopEnding::Closed, EShopEnding::FailedRedemption, EShopEnding::Returned, EShopEnding::Redeemed };
    if (Run && Section >= 0 && Section < UE_ARRAY_COUNT(Endings))
    {
        FEndingData Row;
        if (Run->GetEndingInfo(Endings[Section], Row)) return Row.Title;
    }
    switch (Section)
    {
    case 5: return FText::FromString(TEXT("污染从哪里来"));
    case 6: return FText::FromString(TEXT("污染阶段与风险"));
    case 7: return FText::FromString(TEXT("怎样用律令控制污染"));
    case 8: return FText::FromString(TEXT("七条律令：效果与代价"));
    case 9: return FText::FromString(TEXT("达成结局的准备"));
    default: return FText::GetEmpty();
    }
}

FText UShopPresentationLibrary::GetGameGuideText(const UObject* Context, int32 Section)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(Context);
    if (!Run) return FText::GetEmpty();
    const FRunRules R = Run->GetRunRules();
    FEndingData Closed, Empty, Truth, Redeemed;
    Run->GetEndingInfo(EShopEnding::Closed, Closed);
    Run->GetEndingInfo(EShopEnding::FailedRedemption, Empty);
    Run->GetEndingInfo(EShopEnding::Returned, Truth);
    Run->GetEndingInfo(EShopEnding::Redeemed, Redeemed);
    FString Text;
    switch (Section)
    {
    case 0:
        Text = FString::Printf(TEXT("任意一天，污染达到 %d，立即被污染吞噬。\n这是最高优先级结局：不必等到第 %d 天，也不能在达到上限后再用律令挽救。"), R.PollutionLimit, R.MaxDays); break;
    case 1:
        Text = FString::Printf(TEXT("连续 %d 次夜间结算后资金为负数，书店关门，可在期末前触发。\n某次夜结资金恢复到 0 或以上，连续计数清零；若同时污染达到 %d，优先触发失控。"), Closed.NegativeDaysRequired, R.PollutionLimit); break;
    case 2:
        Text = FString::Printf(TEXT("完成第 %d 天夜间结算并点击继续时，资金少于 %d，进入空书架结局。\n前提是此前没有因污染失控或连续负资金而提前结束。"), R.MaxDays, Empty.MinMoney); break;
    case 3:
        Text = FString::Printf(TEXT("完成第 %d 天时，同时满足：资金 ≥ %d、启蒙 ≥ %d、污染 < %d。\n在最终询问中选择“将真相归还给众人”，达成真结局。确认后支付一次 %d 赎身金。"), R.MaxDays, Truth.MinMoney, Truth.MinEnlighten, Truth.MaxPollutionExclusive, Truth.RedemptionCost); break;
    case 4:
        Text = FString::Printf(TEXT("完成第 %d 天时资金 ≥ %d，但启蒙不足 %d 或污染 ≥ %d，会赎身离场。\n即使已满足真结局条件，选择“赎身离开城市”也进入此结局。支付一次 %d 赎身金；中途攒够资金不会自动结束。"), R.MaxDays, Redeemed.MinMoney, Truth.MinEnlighten, Truth.MaxPollutionExclusive, Redeemed.RedemptionCost); break;
    case 5:
        Text = FString::Printf(TEXT("翻阅里书：每本每晚一次，基础污染 +%d、灵能 +%d；%d%% 概率获得未收集的历史残页，获得时启蒙 +%d。\n成功售卖上架的里书：基础污染 +%d，同时按书籍配置获得高额资金和启蒙。\n黑市购买里书：每册基础污染 +%d；单纯上架或撤回不增加污染。\n夜结基础自然降低 %d 污染。律令、反噬、篡改书或污染返架书可能改变最终数值。"), R.PollutionOnRead, R.ReadPsychicGain, FMath::RoundToInt(R.HistoryFragmentChance * 100), R.HistoryFragmentEnlightenGain, R.PollutionOnSell, R.MarketBookPollution, R.PollutionDecay); break;
    case 6:
        Text = FString::Printf(TEXT("0–%d｜安全：保持低污染，为真结局留出余量。\n%d–%d｜轻度：有未封印里书时，夜晚可能额外增加 %d 污染；相关律令可以抑制蔓延。\n%d–%d｜中度：顾客耐心消耗速度变为 %.1f 倍，被污染顾客可能加入白天队列。\n%d–%d｜重度：宽限 %d 个逻辑回合后仍处于重度且未满足金律宽限条件，额外增加 %d 污染。应尽快降级。\n达到 %d｜立即失控，优先于律令或残页弹窗。"), R.LightThreshold-1, R.LightThreshold, R.MediumThreshold-1, R.LightSpreadPerNight, R.MediumThreshold, R.HeavyThreshold-1, 1.f+R.PatienceDropRatePolluted, R.HeavyThreshold, R.PollutionLimit-1, R.HeavyGraceTurns, R.HeavyPenalty, R.PollutionLimit); break;
    case 7:
        Text = FString::Printf(TEXT("表书店与里书店都可从“律令”入口打开选择页。污染跨入更高阶段时，也会请求打开律令页；若同时获得残页，先阅读残页。\n选择卡牌只查看预览；点击“确定”才消耗灵能、支付该卡代价并应用效果。有些律令立即减污，有些改变后续污染增长或夜间衰减；点击“介绍”可查看完整效果、代价与漏洞。\n候选受当前污染阶段、资源和冷却限制，不保证每次都能释放所有卡。没有可用普通律令时，提供应急镇定：资金 -%d、污染 -%d，不耗灵能。\n律令有持续效果、延迟反噬和冷却。夜结会推进逻辑回合，当前规则下污染升级也会推进；阅读本介绍或停留在律令页不推进回合。\n打开律令页或本介绍时，顾客到场、耐心和伪装顾客的污染计时都会暂停，关闭后继续。"), R.EmergencyMoneyCost, R.EmergencyPollutionCut); break;
    case 8:
        for (const TCHAR* Id : { TEXT("bronze_01"), TEXT("bronze_02"), TEXT("bronze_03"), TEXT("silver_01"), TEXT("silver_02"), TEXT("gold_01"), TEXT("gold_02") })
        {
            FDecreeData D;
            if (!Run->GetDecreeInfo(Id, D) || !D.bEnabled) continue;
            if (!Text.IsEmpty()) Text += TEXT("\n\n");
            Text += FString::Printf(TEXT("%s｜灵能 %d · 即时减污 %d\n%s\n代价：%s\n反噬：%s"), *D.DisplayName.ToString(), D.PsychicCost, D.PollutionCut, *D.EffectText.ToString(), *D.CostText.ToString(), *D.LoopholeText.ToString());
            if (FName(Id)==TEXT("gold_02")) Text += TEXT("\n说明：重置的是当前阶段的预警／宽限记录，不改变污染分段的数值阈值。");
        }
        break;
    case 9:
        Text = FString::Printf(TEXT("期末指完成第 %d 天夜间结算后点击“继续”，不会进入第 %d 天。结局判断使用支付赎身金之前的资金。\n获得一张新残页增加 %d 启蒙；成功售卖里书也会增加启蒙，具体以书架说明为准。获取收益时同时留意污染，使用律令控制风险。\n此页只供查阅，不会推进天数、消耗灵能、抽取残页或重新开始游戏。"), R.MaxDays, R.MaxDays+1, R.HistoryFragmentEnlightenGain); break;
    default: break;
    }
    return FText::FromString(Text);
}
