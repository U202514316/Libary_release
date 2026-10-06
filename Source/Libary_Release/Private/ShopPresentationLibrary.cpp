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
        return FText::FromString(FString::Printf(TEXT("灵能 %d / %d"), Snapshot.Psychic, Snapshot.PsychicMax));
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
        return FText::FromString(FString::Printf(TEXT("%d / %d"), Snapshot.Psychic, Snapshot.PsychicMax));
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
        Description += TEXT("\n在里书店上架后，供表店的秘密顾客购买。");
    else Description += FString::Printf(TEXT("  ·  进货价 %d"), Book.Cost);
    if (Book.SaleEnlightenYield > 0)
        Description += FString::Printf(TEXT("\n售出后有 %d%% 概率获得 %d 点启蒙。"),
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

FText UShopPresentationLibrary::GetEndingTitle(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    FEndingData Ending;
    if (Run->GetEndingInfo(Snapshot.Ending, Ending)) return Ending.Title;
    switch (Snapshot.Ending)
    {
    case EShopEnding::Closed: return FText::FromString(TEXT("关门"));
    case EShopEnding::PollutionReleased: return FText::FromString(TEXT("污染释放"));
    case EShopEnding::Returned: return FText::FromString(TEXT("归还"));
    case EShopEnding::Cycle: return FText::FromString(TEXT("守旧循环"));
    default: return FText::GetEmpty();
    }
}

FText UShopPresentationLibrary::GetEndingText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
    FEndingData Ending;
    return Run->GetEndingInfo(Snapshot.Ending, Ending) ? Ending.Text : Snapshot.EndMessage;
}

FText UShopPresentationLibrary::GetEndingConditionText(const UObject* WorldContextObject)
{
    const UShopRunSubsystem* Run = ShopPresentation::Resolve(WorldContextObject);
    if (!Run) return FText::GetEmpty();
    const FRunSnapshot Snapshot = Run->GetSnapshot_Implementation();
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
    }
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
    case EGamePhase::Restock: case EGamePhase::Market: return 6;
    case EGamePhase::Inside: case EGamePhase::InsideSell: case EGamePhase::History: return 7;
    case EGamePhase::Calm: return 8;
    case EGamePhase::NightEnd: return 9;
    case EGamePhase::End: return 10;
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
