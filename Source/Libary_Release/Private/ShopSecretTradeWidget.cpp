#include "ShopSecretTradeWidget.h"
#include "ShopRunSubsystem.h"
#include "ShopService.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
    FText Text(const TCHAR* Value) { return FText::FromString(Value); }

    const TCHAR* BookTypeName(EBookType Type)
    {
        switch (Type)
        {
        case EBookType::Novel: return TEXT("小说");
        case EBookType::Poem: return TEXT("诗集");
        case EBookType::History: return TEXT("历史");
        case EBookType::Secret: return TEXT("秘密书");
        default: return TEXT("书籍");
        }
    }

    const TCHAR* ResultText(EShopActionResult Code)
    {
        switch (Code)
        {
        case EShopActionResult::Sold: return TEXT("交易成功。");
        case EShopActionResult::Opened: return TEXT("请选择一本书。");
        case EShopActionResult::WrongBook: return TEXT("书籍不符合这位顾客的需求。");
        case EShopActionResult::NoMatch: return TEXT("没有符合需求的可售库存，顾客状态已刷新。");
        case EShopActionResult::OutOfStock: return TEXT("没有可售库存，请先检查库存与上架数量。");
        case EShopActionResult::InsufficientMoney: return TEXT("资金不足。");
        case EShopActionResult::InsufficientPsychic: return TEXT("灵能不足。");
        case EShopActionResult::Expired: return TEXT("顾客耐心已耗尽。");
        case EShopActionResult::InvalidPhase: return TEXT("当前阶段不能执行此操作。");
        case EShopActionResult::InvalidId: return TEXT("目标已失效，请按当前界面重新操作。");
        case EShopActionResult::InvalidConfig: return TEXT("配置无效，请检查正式数据表。");
        case EShopActionResult::AlreadyDone: return TEXT("此操作已经完成。");
        case EShopActionResult::Unavailable: return TEXT("当前无法执行此操作。");
        case EShopActionResult::Cancelled: return TEXT("已取消选书。");
        default: return TEXT("操作未完成，请检查当前阶段与资源。");
        }
    }

    bool IsTrading(EGamePhase Phase)
    {
        return Phase == EGamePhase::Day || Phase == EGamePhase::NightShop;
    }

    bool IsSelling(EGamePhase Phase)
    {
        return Phase == EGamePhase::Sell || Phase == EGamePhase::NightSell;
    }
}

void UShopSecretTradeClickHandler::HandleClicked()
{
    if (Owner.IsValid()) Owner->ExecuteAction(Action, BookId);
}

void UShopSecretTradeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (UGameInstance* Instance = GetGameInstance()) Run = Instance->GetSubsystem<UShopRunSubsystem>();
    BuildInterface();
    if (Run) RefreshShop_Implementation(IShopService::Execute_GetSnapshot(Run));
    else ShowMessage(Text(TEXT("无法获取经营服务，请使用书店 PlayerController 启动。")), false);
    // Registration belongs exclusively to ShopPlayerController::AttachShopView.
}

UTextBlock* UShopSecretTradeWidget::AddText(UVerticalBox* Parent, const FText& Value, int32 FontSize)
{
    UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(Value);
    Label->SetAutoWrapText(true);
    FSlateFontInfo Font = Label->GetFont();
    Font.Size = FontSize;
    Label->SetFont(Font);
    Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.89f, 0.81f)));
    Parent->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 4.f, 0.f, 6.f));
    return Label;
}

void UShopSecretTradeWidget::AddAction(UWrapBox* Parent, const TCHAR* Label, EShopSecretTradeAction Action, FName BookId)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>();
    Button->SetBackgroundColor(FLinearColor(0.24f, 0.31f, 0.36f));
    UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>();
    Caption->SetText(Text(Label));
    FSlateFontInfo Font = Caption->GetFont();
    Font.Size = 19;
    Caption->SetFont(Font);
    Caption->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    UButtonSlot* ButtonSlot = CastChecked<UButtonSlot>(Button->AddChild(Caption));
    ButtonSlot->SetPadding(FMargin(12.f, 7.f));
    Parent->AddChildToWrapBox(Button)->SetPadding(FMargin(0.f, 4.f, 12.f, 6.f));
    UShopSecretTradeClickHandler* Handler = NewObject<UShopSecretTradeClickHandler>(this);
    Handler->Owner = this;
    Handler->Button = Button;
    Handler->Action = Action;
    Handler->BookId = BookId;
    Button->OnClicked.AddDynamic(Handler, &UShopSecretTradeClickHandler::HandleClicked);
    ClickHandlers.Add(Handler);
}

void UShopSecretTradeWidget::BuildInterface()
{
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrushColor(FLinearColor(0.045f, 0.055f, 0.065f, 1.f));
    Background->SetPadding(FMargin(28.f, 20.f));
    WidgetTree->RootWidget = Background;
    UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
    Background->SetContent(Scroll);
    UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Main);
    AddText(Main, Text(TEXT("表里书店 · 经营与秘密书上架")), 28);
    StatusText = AddText(Main, FText());
    PhaseText = AddText(Main, FText(), 22);
    MessageText = AddText(Main, Text(TEXT("所有交易在表书店进行；里书店用于管理秘密书。")));
    UWrapBox* NavigationActions = WidgetTree->ConstructWidget<UWrapBox>();
    Main->AddChildToVerticalBox(NavigationActions);
    AddAction(NavigationActions, TEXT("开始新局 / 重新开始"), EShopSecretTradeAction::NewRun);
    AddAction(NavigationActions, TEXT("继续"), EShopSecretTradeAction::Continue);
    AddAction(NavigationActions, TEXT("结束白天"), EShopSecretTradeAction::EndDay);
    AddAction(NavigationActions, TEXT("进入 / 返回里书店"), EShopSecretTradeAction::OpenInside);
    AddAction(NavigationActions, TEXT("选择 / 返回进货"), EShopSecretTradeAction::OpenRestock);
    AddAction(NavigationActions, TEXT("回到表书店夜间营业"), EShopSecretTradeAction::OpenTableShop);
    AddAction(NavigationActions, TEXT("结束夜晚"), EShopSecretTradeAction::EndNight);
    AddAction(NavigationActions, TEXT("跳过本次安抚并继续"), EShopSecretTradeAction::SkipDecree);
    AddAction(NavigationActions, TEXT("退出游戏"), EShopSecretTradeAction::Quit);

    CustomerPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    Main->AddChildToVerticalBox(CustomerPanel)->SetPadding(FMargin(0.f, 16.f, 0.f, 12.f));
    CustomerText = AddText(CustomerPanel, FText(), 22);
    UWrapBox* CustomerActions = WidgetTree->ConstructWidget<UWrapBox>();
    CustomerPanel->AddChildToVerticalBox(CustomerActions);
    AddAction(CustomerActions, TEXT("接待 / 选择书籍"), EShopSecretTradeAction::BeginSell);
    AddAction(CustomerActions, TEXT("取消选书"), EShopSecretTradeAction::CancelSell);
    AddAction(CustomerActions, TEXT("观察顾客"), EShopSecretTradeAction::Observe);
    AddAction(CustomerActions, TEXT("拒绝当前顾客"), EShopSecretTradeAction::Reject);

    InventoryPanel = WidgetTree->ConstructWidget<UVerticalBox>();
    Main->AddChildToVerticalBox(InventoryPanel);
    AddText(InventoryPanel, Text(TEXT("书籍与库存")), 24);
    EnsureBookRows();
}

void UShopSecretTradeWidget::EnsureBookRows()
{
    if (bInventoryBuilt || !Run || !InventoryPanel) return;
    const TArray<FName> Ids = Run->GetBookIds();
    if (Ids.IsEmpty()) return;
    for (FName Id : Ids)
    {
        FBookData Data;
        int32 Stock = 0;
        if (!IShopService::Execute_GetBookInfo(Run, Id, Data, Stock)) continue;
        FShopSecretTradeBookRow Row;
        Row.BookId = Id;
        Row.Data = Data;
        Row.Panel = WidgetTree->ConstructWidget<UVerticalBox>();
        InventoryPanel->AddChildToVerticalBox(Row.Panel)->SetPadding(FMargin(0.f, 10.f, 0.f, 12.f));
        AddText(Row.Panel, FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  基础售价 %d"),
            *Data.DisplayName.ToString(), BookTypeName(Data.BookType), Data.Price)), 21);
        Row.StockText = AddText(Row.Panel, FText(), 18);
        UWrapBox* Actions = WidgetTree->ConstructWidget<UWrapBox>();
        Row.Panel->AddChildToVerticalBox(Actions);
        AddAction(Actions, TEXT("出售一本"), EShopSecretTradeAction::Sell, Id);
        if (Data.BookType == EBookType::Secret)
        {
            AddAction(Actions, TEXT("上架一本到表店"), EShopSecretTradeAction::ListSecret, Id);
            AddAction(Actions, TEXT("撤架一本回里店"), EShopSecretTradeAction::UnlistSecret, Id);
        }
        else AddAction(Actions, TEXT("进货一本"), EShopSecretTradeAction::Restock, Id);
        BookRows.Add(Row);
    }
    bInventoryBuilt = true;
}

int32 UShopSecretTradeWidget::FindHead(const TArray<FCustomerRuntime>& Customers) const
{
    for (int32 Index = 0; Index < Customers.Num(); ++Index)
        if (!Customers[Index].bServed) return Index;
    return INDEX_NONE;
}

void UShopSecretTradeWidget::RefreshShop_Implementation(const FRunSnapshot& Snapshot)
{
    if (!StatusText || !Run) return;
    EnsureBookRows();
    const EGamePhase Phase = Snapshot.Phase;
    StatusText->SetText(FText::FromString(FString::Printf(
        TEXT("第 %d / %d 天    资金 %d    灵能 %d / %d    污染 %d    启蒙 %d    总库存 %d\n今日收入 %d    今日支出 %d"),
        Snapshot.Day, Snapshot.MaxDays, Snapshot.Money, Snapshot.Psychic, Snapshot.PsychicMax,
        Snapshot.Pollution, Snapshot.Enlighten, Snapshot.TotalStock, Snapshot.TodayIncome, Snapshot.TodayExpense)));

    const TCHAR* PhaseDescription = TEXT("当前阶段未包含在本次简易交易界面中。");
    switch (Phase)
    {
    case EGamePhase::Boot: PhaseDescription = TEXT("待开始：点击开始新局。秘密书需要先在里店上架，秘密顾客只在夜间的表店出现。"); break;
    case EGamePhase::Day: PhaseDescription = TEXT("表书店 · 白天营业：接待顾客并出售普通书籍。"); break;
    case EGamePhase::Sell: PhaseDescription = TEXT("表书店 · 正在选书：点击书籍的出售按钮，或取消选书。"); break;
    case EGamePhase::DayEnd: PhaseDescription = TEXT("日间结算：核对收支后继续，选择今晚的活动。"); break;
    case EGamePhase::DuskChoice: PhaseDescription = TEXT("夜间选择：进货或进入里书店；完成后可以回表店营业。"); break;
    case EGamePhase::Restock: PhaseDescription = TEXT("进货：每次购买一本普通书；完成后回表店夜间营业，或结束夜晚。"); break;
    case EGamePhase::Inside: PhaseDescription = TEXT("里书店 · 秘密书管理：此处没有顾客。上架后回表店，才能把秘密书卖给秘密顾客。"); break;
    case EGamePhase::NightShop: PhaseDescription = TEXT("表书店 · 夜间营业：秘密顾客只买已上架的秘密书；其他顾客购买普通书。"); break;
    case EGamePhase::NightSell: PhaseDescription = TEXT("表书店 · 夜间选书：出售对应类别的可售书籍；取消后可继续接待或返回里店。"); break;
    case EGamePhase::Calm: PhaseDescription = TEXT("污染触发安抚。本次简易界面只提供跳过；跳过后仍按原有污染规则结算后果。"); break;
    case EGamePhase::NightEnd: PhaseDescription = TEXT("夜间结算完成：点击继续进入下一天或结局。"); break;
    case EGamePhase::End: PhaseDescription = TEXT("本局结束。可以重新开始。"); break;
    default: break;
    }
    PhaseText->SetText(Text(PhaseDescription));
    if (Phase == EGamePhase::End)
    {
        FEndingData Ending;
        if (Run->GetEndingInfo(Snapshot.Ending, Ending))
            PhaseText->SetText(FText::FromString(Ending.Title.ToString() + TEXT("\n") + Ending.Text.ToString()));
        else if (!Snapshot.EndMessage.IsEmpty()) PhaseText->SetText(Snapshot.EndMessage);
    }

    const TArray<FCustomerRuntime> Customers = IShopService::Execute_GetCustomers(Run);
    const int32 Head = FindHead(Customers);
    const bool bHasCustomer = Customers.IsValidIndex(Head);
    const bool bTradingOrSelling = IsTrading(Phase) || IsSelling(Phase);
    CustomerPanel->SetVisibility(bTradingOrSelling ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (bHasCustomer)
    {
        int32 Remaining = 0;
        for (const FCustomerRuntime& Customer : Customers) if (!Customer.bServed) ++Remaining;
        const FCustomerRuntime& Customer = Customers[Head];
        CustomerText->SetText(FText::FromString(FString::Printf(TEXT("当前顾客 #%d  ·  待接待 %d 人  ·  耐心 %d / %d 秒\n%s"),
            Head + 1, Remaining, FMath::CeilToInt(FMath::Max(0.f, Customer.Patience)),
            FMath::CeilToInt(Customer.MaxPatience), *Run->BuildCustomerNeedText(Head).ToString())));
    }
    else CustomerText->SetText(Text(TEXT("当前没有待接待顾客，可以结束营业。")));

    const int32 Active = Run->GetActiveCustomerIndex();
    const bool bSecretBuyer = IsSelling(Phase) && Customers.IsValidIndex(Active) && Customers[Active].Kind == ECustomerKind::Secret;
    const bool bInventoryVisible = bTradingOrSelling || Phase == EGamePhase::Inside || Phase == EGamePhase::Restock;
    InventoryPanel->SetVisibility(bInventoryVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    for (FShopSecretTradeBookRow& Row : BookRows)
    {
        FBookRuntime Book;
        Run->GetBookRuntime(Row.BookId, Book);
        const bool bSecret = Row.Data.BookType == EBookType::Secret;
        bool bShow = Phase == EGamePhase::Inside ? bSecret : Phase == EGamePhase::NightShop ? true : !bSecret;
        if (IsSelling(Phase)) bShow = bSecret == bSecretBuyer;
        Row.Panel->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Row.StockText->SetText(FText::FromString(bSecret
            ? FString::Printf(TEXT("已拥有 %d 本    里店存放 %d 本    表店已上架 %d 本"), Book.Stock, Book.StoredCopies, Book.ListedCopies)
            : FString::Printf(TEXT("现有库存 %d 本    进货价 %d"), Book.Stock, Row.Data.Cost)));
    }

    for (UShopSecretTradeClickHandler* Handler : ClickHandlers)
    {
        bool bVisible = false;
        bool bEnabled = true;
        FBookRuntime Book;
        FBookData Data;
        int32 Stock = 0;
        const bool bBookAction = !Handler->BookId.IsNone();
        if (bBookAction)
        {
            bEnabled = Run->GetBookRuntime(Handler->BookId, Book) && IShopService::Execute_GetBookInfo(Run, Handler->BookId, Data, Stock);
        }
        switch (Handler->Action)
        {
        case EShopSecretTradeAction::NewRun: bVisible = Phase == EGamePhase::Boot || Phase == EGamePhase::End; break;
        case EShopSecretTradeAction::Continue: bVisible = Phase == EGamePhase::DayEnd || Phase == EGamePhase::NightEnd; break;
        case EShopSecretTradeAction::EndDay: bVisible = Phase == EGamePhase::Day; break;
        case EShopSecretTradeAction::OpenInside: bVisible = Phase == EGamePhase::DuskChoice || (Phase == EGamePhase::NightShop && Snapshot.NightChoice == ENightChoice::Inside); break;
        case EShopSecretTradeAction::OpenRestock: bVisible = Phase == EGamePhase::DuskChoice || (Phase == EGamePhase::NightShop && Snapshot.NightChoice == ENightChoice::Restock); break;
        case EShopSecretTradeAction::OpenTableShop: bVisible = Phase == EGamePhase::Inside || Phase == EGamePhase::Restock; break;
        case EShopSecretTradeAction::EndNight: bVisible = Phase == EGamePhase::Inside || Phase == EGamePhase::Restock || Phase == EGamePhase::NightShop; break;
        case EShopSecretTradeAction::BeginSell: bVisible = IsTrading(Phase); bEnabled = bHasCustomer; break;
        case EShopSecretTradeAction::CancelSell: bVisible = IsSelling(Phase); break;
        case EShopSecretTradeAction::Observe: bVisible = bTradingOrSelling; bEnabled = bHasCustomer; break;
        case EShopSecretTradeAction::Reject: bVisible = bTradingOrSelling; bEnabled = bHasCustomer; break;
        case EShopSecretTradeAction::Sell:
            bVisible = IsSelling(Phase) && (Data.BookType == EBookType::Secret) == bSecretBuyer;
            bEnabled &= Data.BookType == EBookType::Secret ? Book.ListedCopies > 0 : Book.Stock > 0;
            break;
        case EShopSecretTradeAction::Restock: bVisible = Phase == EGamePhase::Restock; bEnabled &= Snapshot.Money >= Data.Cost; break;
        case EShopSecretTradeAction::ListSecret: bVisible = Phase == EGamePhase::Inside; bEnabled &= Book.StoredCopies > 0; break;
        case EShopSecretTradeAction::UnlistSecret: bVisible = Phase == EGamePhase::Inside; bEnabled &= Book.ListedCopies > 0; break;
        case EShopSecretTradeAction::SkipDecree: bVisible = Phase == EGamePhase::Calm; break;
        case EShopSecretTradeAction::Quit: bVisible = Phase == EGamePhase::Boot || Phase == EGamePhase::End; break;
        }
        Handler->Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        Handler->Button->SetIsEnabled(bVisible && bEnabled);
    }
}

void UShopSecretTradeWidget::ExecuteAction(EShopSecretTradeAction Action, FName BookId)
{
    if (bHandlingClick || !Run) return;
    TGuardValue<bool> Guard(bHandlingClick, true);
    if (Action == EShopSecretTradeAction::Quit)
    {
        UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
        return;
    }
    FShopCommandResult Result;
    const TCHAR* Success = TEXT("操作成功。");
    const int32 Head = FindHead(IShopService::Execute_GetCustomers(Run));
    switch (Action)
    {
    case EShopSecretTradeAction::NewRun:
        IShopService::Execute_RequestNewRun(Run); Result = Run->GetLastResult(); Success = TEXT("新的一局已开始。"); break;
    case EShopSecretTradeAction::Continue:
        IShopService::Execute_RequestContinue(Run); Result = Run->GetLastResult(); Success = TEXT("已继续。"); break;
    case EShopSecretTradeAction::EndDay:
        IShopService::Execute_RequestEndDay(Run); Result = Run->GetLastResult(); Success = TEXT("白天营业结束。"); break;
    case EShopSecretTradeAction::OpenInside:
        Result = IShopService::Execute_RequestOpenInside(Run); Success = TEXT("已进入里书店，可上架或撤架秘密书。"); break;
    case EShopSecretTradeAction::OpenRestock:
        Result = IShopService::Execute_RequestOpenRestock(Run); Success = TEXT("已进入进货页面。"); break;
    case EShopSecretTradeAction::OpenTableShop:
        Result = IShopService::Execute_RequestOpenTableShop(Run); Success = TEXT("已回到表书店，夜间顾客在这里交易。"); break;
    case EShopSecretTradeAction::EndNight:
        Result = IShopService::Execute_RequestEndNight(Run); Success = TEXT("已提交夜间结算。"); break;
    case EShopSecretTradeAction::BeginSell:
        IShopService::Execute_RequestBeginSell(Run, Head); Result = Run->GetLastResult(); Success = TEXT("请从下方书籍中选择一本出售。"); break;
    case EShopSecretTradeAction::CancelSell:
        IShopService::Execute_RequestCancelSell(Run); Result = Run->GetLastResult(); Success = TEXT("已取消选书，顾客耐心继续计时。"); break;
    case EShopSecretTradeAction::Observe:
        Result = IShopService::Execute_RequestObserveCustomer(Run, Head);
        // Successful observation is described by ShowObserveResult during the command.
        if (Result.bSucceeded) return;
        break;
    case EShopSecretTradeAction::Reject:
        Result = IShopService::Execute_RequestRejectCustomer(Run, Head); Success = TEXT("已拒绝这位顾客。"); break;
    case EShopSecretTradeAction::Sell:
        IShopService::Execute_RequestSell(Run, BookId); Result = Run->GetLastResult(); Success = TEXT("已售出一本，实际收入与库存见上方状态。"); break;
    case EShopSecretTradeAction::Restock:
        IShopService::Execute_RequestRestock(Run, BookId); Result = Run->GetLastResult(); Success = TEXT("已进货一本。"); break;
    case EShopSecretTradeAction::ListSecret:
        Result = IShopService::Execute_RequestListSecretBook(Run, BookId); Success = TEXT("已上架一本到表店；所有权总库存不变。"); break;
    case EShopSecretTradeAction::UnlistSecret:
        Result = IShopService::Execute_RequestUnlistSecretBook(Run, BookId); Success = TEXT("已撤架一本回里店；所有权总库存不变。"); break;
    case EShopSecretTradeAction::SkipDecree:
        Result = IShopService::Execute_RequestSkipDecree(Run); Success = TEXT("已跳过本次安抚，按经营规则继续。"); break;
    default: return;
    }
    // Snapshot reads are presentation only. All mutation above occurs in a button callback.
    ReportResult(Result, Success);
    RefreshShop_Implementation(IShopService::Execute_GetSnapshot(Run));
}

void UShopSecretTradeWidget::ShowMessage(const FText& Value, bool bSuccess)
{
    if (!MessageText) return;
    MessageText->SetText(Value);
    MessageText->SetColorAndOpacity(FSlateColor(bSuccess ? FLinearColor(0.65f, 0.88f, 0.69f) : FLinearColor(1.f, 0.63f, 0.47f)));
}

void UShopSecretTradeWidget::ReportResult(const FShopCommandResult& Result, const TCHAR* SuccessText)
{
    ShowMessage(!Result.Message.IsEmpty() ? Result.Message : Text(Result.bSucceeded ? SuccessText : ResultText(Result.Code)), Result.bSucceeded);
}

void UShopSecretTradeWidget::ResolveCustomer_Implementation(int32 CustomerIndex, EShopActionResult Result)
{
    ShowMessage(FText::FromString(FString::Printf(TEXT("顾客 #%d：%s"), CustomerIndex + 1, ResultText(Result))), Result == EShopActionResult::Sold);
}

void UShopSecretTradeWidget::NewDay_Implementation(int32 Day)
{
    ShowMessage(FText::FromString(FString::Printf(TEXT("第 %d 天开始。"), Day)));
}

void UShopSecretTradeWidget::OpenCalmPanel_Implementation(const TArray<FName>& Candidates, const FRunSnapshot& Snapshot)
{
    RefreshShop_Implementation(Snapshot);
}

void UShopSecretTradeWidget::ShowDecreeResult_Implementation(FName DecreeId, const FShopCommandResult& Result)
{
    ReportResult(Result, TEXT("律令已执行。"));
}

void UShopSecretTradeWidget::ShowObserveResult_Implementation(int32 CustomerIndex, const FCustomerRuntime& Customer)
{
    ShowMessage(FText::FromString(FString::Printf(TEXT("观察顾客 #%d：%s%s"), CustomerIndex + 1,
        Customer.bPolluted ? TEXT("发现污染迹象。") : TEXT("未发现污染迹象。"),
        Customer.bFake ? TEXT("这是假顾客，可以拒绝。") : TEXT(""))));
}

void UShopSecretTradeWidget::ShowHistoryPanel_Implementation(FName EventId, const FEventData& Event)
{
    ShowMessage(Text(TEXT("历史功能未包含在本次简易交易界面中。")), false);
}

void UShopSecretTradeWidget::ShowEnding_Implementation(EShopEnding Ending, const FRunSnapshot& Snapshot)
{
    RefreshShop_Implementation(Snapshot);
}

void UShopSecretTradeWidget::ShowOwlTip_Implementation(FName LineId, const FText& Value)
{
    ShowMessage(Value);
}

void UShopSecretTradeWidget::OpenMarket_Implementation(const TArray<FName>& ItemIds, const FRunSnapshot& Snapshot)
{
    ShowMessage(Text(TEXT("市场功能未包含在本次简易交易界面中。")), false);
}
