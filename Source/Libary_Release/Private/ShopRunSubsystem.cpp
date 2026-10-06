#include "ShopRunSubsystem.h"
#include "ShopSettings.h"
#include "ShopValidation.h"
#include "ShopEconomy.h"
#include "ShopCustomers.h"
#include "ShopDecrees.h"
#include "ShopStory.h"
#include "ShopView.h"
#include "Engine/World.h"

namespace
{
    template<typename T> bool ReadTable(UDataTable* Table, TMap<FName, T>& Rows, bool Required, const TCHAR* Label, FText& Error)
    {
        if (!Table)
        {
            if (!Required) return true;
            Error = FText::FromString(FString(Label) + TEXT(": missing DataTable")); return false;
        }
        if (Table->GetRowStruct() != T::StaticStruct())
        { Error = FText::FromString(FString(Label) + TEXT(": incorrect native RowStruct")); return false; }
        for (FName Name : Table->GetRowNames())
        {
            const T* Row = Table->FindRow<T>(Name, Label);
            if (!Row) { Error = FText::FromString(FString(Label) + TEXT(": unreadable row")); return false; }
            Rows.Add(Name, *Row);
        }
        return true;
    }
    bool TradingPhase(EGamePhase Phase) { return Phase == EGamePhase::Day || Phase == EGamePhase::Inside; }
    bool SellingPhase(EGamePhase Phase) { return Phase == EGamePhase::Sell || Phase == EGamePhase::InsideSell; }
    bool ModalPhase(EGamePhase Phase) { return Phase == EGamePhase::Calm || Phase == EGamePhase::History; }
    FText EndingText(EShopEnding Ending)
    {
        switch (Ending)
        {
        case EShopEnding::Closed: return FText::FromString(TEXT("关门：经营已无法维持。"));
        case EShopEnding::PollutionReleased: return FText::FromString(TEXT("污染释放：污染已突破极限。"));
        case EShopEnding::Returned: return FText::FromString(TEXT("归还：你获得了足够的启蒙，并控制住污染。"));
        case EShopEnding::Cycle: return FText::FromString(TEXT("守旧循环：期限结束，循环仍在继续。"));
        default: return FText();
        }
    }
}

void UShopRunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const UShopSettings* Settings = GetDefault<UShopSettings>();
    ConfigureTables(Settings->Books.LoadSynchronous(), Settings->Customers.LoadSynchronous(), Settings->RunRules.LoadSynchronous(),
        Settings->Decrees.LoadSynchronous(), Settings->Events.LoadSynchronous(), Settings->MarketItems.LoadSynchronous(), Settings->OwlLines.LoadSynchronous(), Settings->RandomSeed, Settings->Endings.LoadSynchronous());
}

void UShopRunSubsystem::Deinitialize()
{
    Views.Reset();
    State = FShopRunState();
    bConfigured = false;
    Super::Deinitialize();
}

bool UShopRunSubsystem::ConfigureTables(UDataTable* Books, UDataTable* Customers, UDataTable* Rules, UDataTable* Decrees,
    UDataTable* Events, UDataTable* Market, UDataTable* Owl, int32 Seed, UDataTable* Endings)
{
    if (bCommitting || (State.Phase != EGamePhase::Boot && State.Phase != EGamePhase::End))
    { Reject(EShopActionResult::InvalidPhase, TEXT("只能在开局前或结束后替换配置。")); return false; }
    FShopCatalog Candidate;
    FText Error;
    TMap<FName, FRunRules> RuleRows;
    const bool Loaded = ReadTable(Books, Candidate.Books, true, TEXT("DT_Books"), Error) &&
        ReadTable(Customers, Candidate.Customers, true, TEXT("DT_Customers"), Error) &&
        ReadTable(Rules, RuleRows, true, TEXT("DT_RunRules"), Error) &&
        ReadTable(Decrees, Candidate.Decrees, true, TEXT("DT_Decrees"), Error) &&
        ReadTable(Events, Candidate.Events, false, TEXT("DT_Events"), Error) &&
        ReadTable(Market, Candidate.MarketItems, false, TEXT("DT_MarketItems"), Error) &&
        ReadTable(Owl, Candidate.OwlLines, false, TEXT("DT_Owl"), Error) &&
        ReadTable(Endings, Candidate.Endings, false, TEXT("DT_Endings"), Error);
    if (!Loaded) { Result(false, EShopActionResult::InvalidConfig, Error); return false; }
    const FRunRules* Default = RuleRows.Find(TEXT("Default"));
    if (!Default) { Reject(EShopActionResult::InvalidConfig, TEXT("DT_RunRules 缺少 Default 行。")); return false; }
    Candidate.Rules = *Default;
    if (!ShopValidation::Validate(Candidate, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return false; }
    // The row name is the identity; a blank duplicated Id field is normalized here.
    for (auto& Pair : Candidate.Decrees) Pair.Value.Id = Pair.Key;
    for (auto& Pair : Candidate.Events) Pair.Value.Id = Pair.Key;
    for (auto& Pair : Candidate.MarketItems) Pair.Value.Id = Pair.Key;
    for (auto& Pair : Candidate.OwlLines) Pair.Value.Id = Pair.Key;
    Catalog = MoveTemp(Candidate);
    ConfiguredSeed = Seed;
    bConfigured = true;
    Result(true, EShopActionResult::Success);
    return true;
}

bool UShopRunSubsystem::ValidateConfig(FText& Error) const
{
    if (!bConfigured) { Error = FText::FromString(TEXT("尚未加载有效配置。")); return false; }
    return ShopValidation::Validate(Catalog, Error);
}

FShopCommandResult UShopRunSubsystem::Result(bool Success, EShopActionResult Code, const FText& Message, FName Id)
{
    FShopCommandResult Response;
    Response.bSucceeded = Success; Response.Code = Code; Response.Message = Message; Response.SubjectId = Id;
    // A listener cannot replace the outer command's result while that command is notifying.
    if (!bCommitting) LastResult = Response;
    return Response;
}

FShopCommandResult UShopRunSubsystem::Reject(EShopActionResult Code, const TCHAR* Message)
{
    return Result(false, Code, FText::FromString(Message));
}

bool UShopRunSubsystem::ReadyForCommand()
{
    if (bCommitting) return false;
    if (!bConfigured) { Reject(EShopActionResult::InvalidConfig, TEXT("数据未配置，请检查项目设置中的 Bookstore 数据表。")); return false; }
    if (State.Phase == EGamePhase::Boot || State.Phase == EGamePhase::End)
    { Reject(EShopActionResult::InvalidPhase, TEXT("请先开始一局游戏。")); return false; }
    return true;
}

bool UShopRunSubsystem::RequestNewRun_Implementation()
{
    if (bCommitting) return false;
    if (State.Phase != EGamePhase::Boot && State.Phase != EGamePhase::End)
    { Reject(EShopActionResult::InvalidPhase, TEXT("当前一局尚未结束，不能覆盖正在进行的经营。")); return false; }
    FText Error;
    if (!ValidateConfig(Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return false; }
    FShopRunState Next;
    ShopEconomy::Reset(Next, Catalog);
    const int32 Seed = ConfiguredSeed < 0 ? FMath::Rand() : ConfiguredSeed;
    Next.Random.Initialize(Seed);
    Next.CosmeticRandom.Initialize(Seed ^ 0x51A70A1);
    Next.Day = 1;
    if (!StartDay(Next, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return false; }
    Commit(MoveTemp(Next), EShopActionResult::Success, NAME_None, INDEX_NONE, true);
    return LastResult.bSucceeded;
}

bool UShopRunSubsystem::StartDay(FShopRunState& Next, FText& Error)
{
    Next.Phase = EGamePhase::Day;
    Next.ResumePhase = EGamePhase::Day;
    Next.NightChoice = ENightChoice::None;
    Next.TodayIncome = 0;
    Next.TodayExpense = 0;
    Next.ActiveCustomer = INDEX_NONE;
    Next.MarketStock.Reset();
    Next.MarketSold.Reset();
    if (!ShopCustomers::Generate(Next, Catalog, EBookLayer::Table, Error)) return false;
    Next.DecreeCandidates.Reset();
    QueueEvents(Next, EShopEventTrigger::OnDayStart);
    return true;
}

FRunSnapshot UShopRunSubsystem::GetSnapshot_Implementation() const
{
    FRunSnapshot Out;
    Out.Day = State.Day; Out.MaxDays = Catalog.Rules.MaxDays; Out.Money = State.Money; Out.Psychic = State.Psychic;
    Out.Pollution = State.Pollution; Out.Enlighten = State.Enlighten; Out.PollutionStage = ShopDecrees::GetStage(State.Pollution, Catalog.Rules);
    Out.TotalStock = ShopEconomy::TotalStock(State); Out.TodayIncome = State.TodayIncome; Out.TodayExpense = State.TodayExpense;
    Out.TotalSold = State.TotalSold; Out.Rent = Catalog.Rules.Rent + State.RentPenalty; Out.Turn = State.Turn; Out.NegativeDays = State.NegativeDays;
    Out.PsychicMax = Catalog.Rules.PsychicMax;
    Out.Phase = State.Phase; Out.NightChoice = State.NightChoice; Out.Ending = State.Ending;
    Out.ActiveDecrees = State.Decrees; Out.Clues = State.Clues; Out.DecreeCandidates = State.DecreeCandidates;
    Out.PendingEventId = State.PendingEventId; Out.EndMessage = EndingText(State.Ending);
    FEndingData EndingData;
    if (GetEndingInfo(State.Ending, EndingData)) Out.EndMessage = EndingData.Text;
    return Out;
}

TArray<FCustomerRuntime> UShopRunSubsystem::GetCustomers_Implementation() const { return State.Customers; }
bool UShopRunSubsystem::GetBookInfo_Implementation(FName BookId, FBookData& BookData, int32& Stock) const
{
    BookData = FBookData(); Stock = 0;
    const FBookData* Found = Catalog.Books.Find(BookId);
    if (!Found) return false;
    BookData = *Found;
    if (const FBookRuntime* Runtime = State.Inventory.Find(BookId)) Stock = Runtime->Stock;
    return true;
}
TArray<FName> UShopRunSubsystem::GetBookIds() const
{
    TArray<FName> Ids; Catalog.Books.GetKeys(Ids); Ids.Sort(FNameLexicalLess()); return Ids;
}
bool UShopRunSubsystem::GetBookRuntime(FName BookId, FBookRuntime& Book) const
{
    Book = FBookRuntime(); if (const FBookRuntime* Found = State.Inventory.Find(BookId)) { Book = *Found; return true; } return false;
}
bool UShopRunSubsystem::GetDecreeInfo(FName Id, FDecreeData& Decree) const
{
    if (Id == FName(TEXT("emergency_calm"))) { Decree = ShopDecrees::GetFallbackDecree(Catalog.Rules); return true; }
    Decree = FDecreeData(); if (const FDecreeData* Found = Catalog.Decrees.Find(Id)) { Decree = *Found; return true; } return false;
}
bool UShopRunSubsystem::CanEnactDecree(FName Id, FText& Reason) const
{
    if (State.Phase != EGamePhase::Calm) { Reason = FText::FromString(TEXT("当前不在镇定阶段。")); return false; }
    return ShopDecrees::CanEnact(State, Catalog, Id, Reason);
}
bool UShopRunSubsystem::GetEndingInfo(EShopEnding Ending, FEndingData& Data) const
{
    Data = FEndingData();
    for (const auto& Pair : Catalog.Endings) if (Pair.Value.Ending == Ending) { Data = Pair.Value; return true; }
    return false;
}
bool UShopRunSubsystem::GetMarketItemInfo(FName Id, FMarketItemData& Item) const
{
    Item = FMarketItemData(); if (const FMarketItemData* Found = Catalog.MarketItems.Find(Id)) { Item = *Found; return true; } return false;
}
bool UShopRunSubsystem::GetEventInfo(FName Id, FEventData& Event) const
{
    Event = FEventData(); if (const FEventData* Found = Catalog.Events.Find(Id)) { Event = *Found; return true; } return false;
}
int32 UShopRunSubsystem::GetActiveCustomerIndex() const { return State.ActiveCustomer; }
FText UShopRunSubsystem::BuildCustomerNeedText(int32 CustomerIndex) const
{
    return State.Customers.IsValidIndex(CustomerIndex) ? ShopCustomers::BuildNeedText(State.Customers[CustomerIndex], Catalog) : FText();
}
bool UShopRunSubsystem::HasMatchingStock(EBookType Type, EBookLayer Layer) const { return ShopEconomy::HasMatchingStock(State, Catalog, Type, Layer); }

EShopActionResult UShopRunSubsystem::RequestBeginSell_Implementation(int32 CustomerIndex)
{
    if (!ReadyForCommand()) return EShopActionResult::Rejected;
    if (!TradingPhase(State.Phase)) return Reject(EShopActionResult::InvalidPhase, TEXT("当前阶段不能开始售卖。" )).Code;
    if (ShopCustomers::Current(State) != CustomerIndex || !State.Customers.IsValidIndex(CustomerIndex))
        return Reject(EShopActionResult::InvalidId, TEXT("只能接待当前队首的顾客。" )).Code;
    FShopRunState Next = State;
    const FCustomerRuntime& Customer = Next.Customers[CustomerIndex];
    if (Customer.bFake) return Reject(EShopActionResult::Unavailable, TEXT("这位顾客不回应交易。可以观察或拒绝，避免污染继续增加。" )).Code;
    if (!ShopEconomy::HasMatchingStock(Next, Catalog, Customer.NeedType, Customer.NeedLayer))
    {
        if (Catalog.Rules.bNoMatchConsumesCustomer)
        { CompleteCustomer(Next, CustomerIndex, EShopActionResult::NoMatch); Commit(MoveTemp(Next), EShopActionResult::NoMatch, NAME_None, CustomerIndex); }
        else Reject(EShopActionResult::NoMatch, TEXT("没有满足需求的现货。"));
        return EShopActionResult::NoMatch;
    }
    Next.ActiveCustomer = CustomerIndex;
    Next.Phase = State.Phase == EGamePhase::Inside ? EGamePhase::InsideSell : EGamePhase::Sell;
    Commit(MoveTemp(Next), EShopActionResult::Opened);
    return LastResult.Code;
}

void UShopRunSubsystem::CompleteCustomer(FShopRunState& Next, int32 Index, EShopActionResult Code)
{
    ShopCustomers::Complete(Next, Index, Code);
    Next.ActiveCustomer = INDEX_NONE;
    if (Next.Phase == EGamePhase::Sell) Next.Phase = EGamePhase::Day;
    else if (Next.Phase == EGamePhase::InsideSell) Next.Phase = EGamePhase::Inside;
}

EShopActionResult UShopRunSubsystem::RequestSell_Implementation(FName BookId)
{
    if (!ReadyForCommand()) return EShopActionResult::Rejected;
    if (!SellingPhase(State.Phase) || !State.Customers.IsValidIndex(State.ActiveCustomer))
        return Reject(EShopActionResult::InvalidPhase, TEXT("请先选择当前顾客。" )).Code;
    FShopRunState Next = State;
    FText Error;
    const int32 Index = Next.ActiveCustomer;
    const EShopActionResult Code = ShopEconomy::Sell(Next, Catalog, BookId, Next.Customers[Index], Error);
    if (Code == EShopActionResult::Sold || (Code == EShopActionResult::WrongBook && Catalog.Rules.bWrongBookConsumesCustomer))
    { CompleteCustomer(Next, Index, Code); Commit(MoveTemp(Next), Code, BookId, Index); }
    else Result(false, Code, Error, BookId);
    return LastResult.Code;
}

bool UShopRunSubsystem::RequestCancelSell_Implementation()
{
    if (!ReadyForCommand()) return false;
    if (!SellingPhase(State.Phase)) { Reject(EShopActionResult::InvalidPhase, TEXT("当前没有选书操作。")); return false; }
    FShopRunState Next = State;
    Next.Phase = Next.Phase == EGamePhase::Sell ? EGamePhase::Day : EGamePhase::Inside;
    Next.ActiveCustomer = INDEX_NONE;
    Commit(MoveTemp(Next), EShopActionResult::Cancelled);
    return LastResult.bSucceeded;
}

FShopCommandResult UShopRunSubsystem::RequestObserveCustomer_Implementation(int32 CustomerIndex)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if ((!TradingPhase(State.Phase) && !SellingPhase(State.Phase)) || ShopCustomers::Current(State) != CustomerIndex || !State.Customers.IsValidIndex(CustomerIndex))
        return Reject(EShopActionResult::InvalidPhase, TEXT("只能观察当前营业中的队首顾客。"));
    FShopRunState Next = State;
    Next.Customers[CustomerIndex].bObserved = true;
    Commit(MoveTemp(Next));
    TGuardValue<bool> Guard(bCommitting, true);
    const auto Receivers = Views;
    for (const auto& View : Receivers) if (View.IsValid()) IShopView::Execute_ShowObserveResult(View.Get(), CustomerIndex, State.Customers[CustomerIndex]);
    return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestRejectCustomer_Implementation(int32 CustomerIndex)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if ((!TradingPhase(State.Phase) && !SellingPhase(State.Phase)) || ShopCustomers::Current(State) != CustomerIndex || !State.Customers.IsValidIndex(CustomerIndex))
        return Reject(EShopActionResult::InvalidPhase, TEXT("当前顾客不可拒绝。"));
    FShopRunState Next = State;
    CompleteCustomer(Next, CustomerIndex, EShopActionResult::Rejected);
    Commit(MoveTemp(Next), EShopActionResult::Success, NAME_None, CustomerIndex);
    return LastResult;
}

bool UShopRunSubsystem::RequestEndDay_Implementation()
{
    if (!ReadyForCommand()) return false;
    if (State.Phase != EGamePhase::Day || (!Catalog.Rules.bAllowEarlyClose && !ShopCustomers::AllServed(State)))
    { Reject(EShopActionResult::InvalidPhase, TEXT("当前不可结束白天营业。")); return false; }
    FShopRunState Next = State;
    FText Error;
    if (Catalog.Rules.RentTiming == ERentTiming::BeforeDusk && !ShopEconomy::PayRent(Next, Catalog.Rules, Error))
    { Result(false, EShopActionResult::Rejected, Error); return false; }
    Next.Phase = EGamePhase::DayEnd;
    Commit(MoveTemp(Next)); return LastResult.bSucceeded;
}

bool UShopRunSubsystem::RequestContinue_Implementation()
{
    if (!ReadyForCommand()) return false;
    if (State.Phase == EGamePhase::DayEnd)
    { FShopRunState Next = State; Next.Phase = EGamePhase::DuskChoice; Commit(MoveTemp(Next)); return LastResult.bSucceeded; }
    if (State.Phase == EGamePhase::Restock || State.Phase == EGamePhase::Inside) return RequestEndNight_Implementation().bSucceeded;
    if (State.Phase == EGamePhase::NightEnd)
    {
        if (Catalog.Rules.bEnableMarket && State.Day % Catalog.Rules.DaysPerWeek == 0 && State.LastMarketDay != State.Day)
            return RequestOpenMarket_Implementation().bSucceeded;
        return RequestNextDay_Implementation();
    }
    if (State.Phase == EGamePhase::Market) return RequestCloseMarket_Implementation().bSucceeded;
    Reject(EShopActionResult::InvalidPhase, TEXT("当前阶段没有可继续的流程。")); return false;
}

FShopCommandResult UShopRunSubsystem::RequestOpenRestock_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::DuskChoice || State.NightChoice != ENightChoice::None)
        return Reject(EShopActionResult::InvalidPhase, TEXT("今晚已经选择活动，不能再次选择进货。"));
    FShopRunState Next = State; Next.NightChoice = ENightChoice::Restock; Next.Phase = EGamePhase::Restock;
    Commit(MoveTemp(Next)); return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestOpenInside_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::DuskChoice || State.NightChoice != ENightChoice::None)
        return Reject(EShopActionResult::InvalidPhase, TEXT("今晚已经选择活动，不能再次进入里书店。"));
    FShopRunState Next = State;
    Next.NightChoice = ENightChoice::Inside; Next.Phase = EGamePhase::Inside;
    if (Catalog.Rules.bRefillSecretOffersEachNight)
        for (const auto& Book : Catalog.Books) if (Book.Value.Layer == EBookLayer::Inside)
            Next.Inventory.FindChecked(Book.Key).AvailableToCollect = Book.Value.CollectOfferPerNight;
    FText Error;
    if (!ShopCustomers::Generate(Next, Catalog, EBookLayer::Inside, Error)) return Result(false, EShopActionResult::InvalidConfig, Error);
    Commit(MoveTemp(Next)); return LastResult;
}

bool UShopRunSubsystem::RequestRestock_Implementation(FName BookId)
{
    if (!ReadyForCommand()) return false;
    if (State.Phase != EGamePhase::Restock || State.NightChoice != ENightChoice::Restock)
    { Reject(EShopActionResult::InvalidPhase, TEXT("只有今晚选择进货后才能采购表书。")); return false; }
    FShopRunState Next = State; FText Error;
    if (!ShopEconomy::Restock(Next, Catalog, BookId, Error)) { Result(false, EShopActionResult::Rejected, Error, BookId); return false; }
    Commit(MoveTemp(Next), EShopActionResult::Success, BookId); return LastResult.bSucceeded;
}

FShopCommandResult UShopRunSubsystem::RequestCollectSecret_Implementation(FName BookId)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Inside) return Reject(EShopActionResult::InvalidPhase, TEXT("请在里书店收取书籍。"));
    FShopRunState Next = State; FText Error;
    if (!ShopEconomy::Collect(Next, Catalog, BookId, Error)) return Result(false, EShopActionResult::Rejected, Error, BookId);
    Commit(MoveTemp(Next), EShopActionResult::Success, BookId); return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestReadSecret_Implementation(FName BookId)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Inside) return Reject(EShopActionResult::InvalidPhase, TEXT("请在里书店翻阅书籍。"));
    FShopRunState Next = State; FText Error;
    if (!ShopEconomy::Read(Next, Catalog, BookId, Error)) return Result(false, EShopActionResult::Rejected, Error, BookId);
    QueueEvents(Next, EShopEventTrigger::OnRead, BookId);
    Commit(MoveTemp(Next), EShopActionResult::Success, BookId); return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestPurify_Implementation(int32 Amount)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Calm || ShopDecrees::GetStage(State.Pollution, Catalog.Rules) != EPollutionStage::Light)
        return Reject(EShopActionResult::InvalidPhase, TEXT("只能在轻度污染的镇定界面净化。"));
    if (Amount <= 0 || Amount > State.Pollution) return Reject(EShopActionResult::Rejected, TEXT("净化数量必须为正且不能超过当前污染。"));
    if (Amount > State.Psychic) return Reject(EShopActionResult::InsufficientPsychic, TEXT("灵能不足。"));
    FShopRunState Next = State; Next.Psychic -= Amount; Next.Pollution -= Amount;
    Next.Phase = Next.ResumePhase; Next.bPendingCalm = false;
    Commit(MoveTemp(Next)); return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestEnactDecree_Implementation(FName DecreeId)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Calm) return Reject(EShopActionResult::InvalidPhase, TEXT("只能在镇定界面颁布律令。"));
    FShopRunState Next = State; FText Error;
    if (!ShopDecrees::Enact(Next, Catalog, DecreeId, Error)) return Result(false, EShopActionResult::Unavailable, Error, DecreeId);
    Next.Phase = Next.ResumePhase; Next.bPendingCalm = false;
    Commit(MoveTemp(Next), EShopActionResult::Success, DecreeId);
    TGuardValue<bool> Guard(bCommitting, true);
    const auto Receivers = Views;
    for (const auto& View : Receivers) if (View.IsValid()) IShopView::Execute_ShowDecreeResult(View.Get(), DecreeId, LastResult);
    return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestSkipDecree_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Calm) return Reject(EShopActionResult::InvalidPhase, TEXT("当前没有镇定请求。"));
    FShopRunState Next = State; Next.Phase = Next.ResumePhase; Next.bPendingCalm = false;
    Commit(MoveTemp(Next)); return LastResult;
}

void UShopRunSubsystem::QueueEvents(FShopRunState& Next, EShopEventTrigger Trigger, FName BookId)
{
    if (!Catalog.Rules.bEnableHistory) return;
    ShopStory::QueueEvents(Next, Catalog, Trigger, BookId);
}

FShopCommandResult UShopRunSubsystem::RequestHistoryChoice_Implementation(EHistoryChoice Choice)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (!Catalog.Rules.bEnableHistory) return Reject(EShopActionResult::Unavailable, TEXT("历史残页按当前开发范围暂未启用。"));
    if (State.Phase != EGamePhase::History) return Reject(EShopActionResult::InvalidPhase, TEXT("当前没有历史事件。"));
    FShopRunState Next = State; FText Error;
    const FName EventId = Next.PendingEventId;
    if (!ShopStory::Witness(Next, Catalog, Choice, Error)) return Result(false, EShopActionResult::Rejected, Error, EventId);
    Next.Phase = Next.ResumePhase;
    Commit(MoveTemp(Next), EShopActionResult::Success, EventId); return LastResult;
}

FShopCommandResult UShopRunSubsystem::RequestOwlTalk_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (ModalPhase(State.Phase)) return Reject(EShopActionResult::InvalidPhase, TEXT("请先处理当前弹窗。"));
    FShopRunState Next = State; FText Line, Error; FName Id;
    if (!ShopStory::OwlTalk(Next, Catalog, Id, Line, Error)) return Result(false, EShopActionResult::Unavailable, Error);
    Commit(MoveTemp(Next), EShopActionResult::Success, Id);
    TGuardValue<bool> Guard(bCommitting, true);
    const auto Receivers = Views;
    for (const auto& View : Receivers) if (View.IsValid()) IShopView::Execute_ShowOwlTip(View.Get(), Id, Line);
    return LastResult;
}

bool UShopRunSubsystem::OpenMarketInternal(FShopRunState& Next, FText& Error) { return ShopStory::OpenMarket(Next, Catalog, Error); }
FShopCommandResult UShopRunSubsystem::RequestOpenMarket_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    FShopRunState Next = State; FText Error;
    if (!OpenMarketInternal(Next, Error)) return Result(false, EShopActionResult::Unavailable, Error);
    Commit(MoveTemp(Next)); return LastResult;
}
FShopCommandResult UShopRunSubsystem::RequestBuyMarketItem_Implementation(FName Id)
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    FShopRunState Next = State; FText Error;
    if (!ShopStory::BuyMarketItem(Next, Catalog, Id, Error)) return Result(false, EShopActionResult::Rejected, Error, Id);
    Commit(MoveTemp(Next), EShopActionResult::Success, Id); return LastResult;
}
FShopCommandResult UShopRunSubsystem::RequestCloseMarket_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Market) return Reject(EShopActionResult::InvalidPhase, TEXT("当前不在黑市。"));
    FShopRunState Next = State; Next.LastMarketDay = Next.Day; Next.Phase = EGamePhase::NightEnd;
    // Leaving the final market resolves the terminal ending without creating Day 36.
    if (Next.Day >= Catalog.Rules.MaxDays) CheckEnding(Next, true);
    else
    {
        ++Next.Day; FText Error;
        if (!StartDay(Next, Error)) return Result(false, EShopActionResult::InvalidConfig, Error);
    }
    Commit(MoveTemp(Next)); return LastResult;
}

bool UShopRunSubsystem::SettleNight(FShopRunState& Next, FText& Error)
{
    if (Next.LastSettledDay == Next.Day) { Error = FText::FromString(TEXT("今天已经结算。")); return false; }
    const FRunRules& R = Catalog.Rules;
    // A settlement point first ages existing decrees; newly expired loopholes affect this night.
    TArray<FName> Triggered;
    if (!ShopDecrees::TickTurn(Next, Catalog, Triggered, Error)) return false;
    if (R.RentTiming == ERentTiming::NightEnd && !ShopEconomy::PayRent(Next, R, Error)) return false;
    const int32 NightMoney = ShopEffects::Sum(Next, EShopEffectType::NightlyMoney);
    if (!ShopEconomy::AddMoney(Next, NightMoney, Error)) return false;
    bool bLooseBook = false;
    for (const auto& Pair : Next.Inventory)
    {
        const FBookData* Book = Catalog.Books.Find(Pair.Key);
        if (!Book || Book->Layer != EBookLayer::Inside) continue;
        bLooseBook |= Pair.Value.AvailableToCollect > 0;
        for (const FSecretBookCopy& Copy : Pair.Value.SecretCopies) bLooseBook |= !Copy.bSealed;
    }
    int64 Pollution = ShopEffects::Sum(Next, EShopEffectType::NightlyPollution);
    if (bLooseBook && ShopDecrees::GetStage(Next.Pollution, R) == EPollutionStage::Light &&
        ShopEffects::Sum(Next, EShopEffectType::BlockLightSpread) <= 0) Pollution += R.LightSpreadPerNight;
    if (Pollution > MAX_int32 || Pollution < MIN_int32) { Error = FText::FromString(TEXT("夜间污染配置溢出。")); return false; }
    if (!ShopEffects::ChangePollution(Next, Catalog, static_cast<int32>(Pollution), Error)) return false;
    const double Decay = static_cast<double>(R.PollutionDecay) * ShopEffects::Multiplier(Next, EShopEffectType::DecayMultiplier);
    if (!FMath::IsFinite(Decay) || Decay < 0.0 || Decay > MAX_int32) { Error = FText::FromString(TEXT("污染衰减配置溢出。")); return false; }
    if (Next.SkipDecayNights > 0) --Next.SkipDecayNights;
    else Next.Pollution = FMath::Max(0, Next.Pollution - FMath::FloorToInt(Decay));
    Next.Modifiers.RemoveAll([](const FShopModifier& Modifier)
    { return Modifier.Type == EShopEffectType::NightIncomeMultiplier || Modifier.Type == EShopEffectType::CustomerCountDelta; });
    Next.NegativeDays = Next.Money < 0 ? Next.NegativeDays + 1 : 0;
    Next.LastSettledDay = Next.Day;
    Next.Phase = EGamePhase::NightEnd;
    QueueEvents(Next, EShopEventTrigger::OnNightEnd);
    return true;
}

bool UShopRunSubsystem::ResolveHeavyWindow(FShopRunState& Next, FText& Error)
{
    const FRunRules& R = Catalog.Rules;
    if (Next.HeavyDueTurn != INDEX_NONE && Next.Turn >= Next.HeavyDueTurn)
    {
        if (Next.Pollution >= R.HeavyThreshold && !(R.bGoldSatisfiesHeavyGrace && Next.bGoldDuringHeavyGrace))
        {
            if (!ShopEffects::ChangePollution(Next, Catalog, R.HeavyPenalty, Error)) return false;
        }
        Next.HeavyDueTurn = Next.Pollution >= R.HeavyThreshold ? Next.Turn + R.HeavyGraceTurns : INDEX_NONE;
        Next.bGoldDuringHeavyGrace = false;
    }
    return true;
}

FShopCommandResult UShopRunSubsystem::RequestEndNight_Implementation()
{
    if (!ReadyForCommand()) return Result(false, EShopActionResult::Rejected, GetLastError());
    if (State.Phase != EGamePhase::Inside && State.Phase != EGamePhase::Restock)
        return Reject(EShopActionResult::InvalidPhase, TEXT("只能在夜间活动结束后结算。"));
    FShopRunState Next = State; FText Error;
    if (!SettleNight(Next, Error)) return Result(false, EShopActionResult::Rejected, Error);
    Commit(MoveTemp(Next)); return LastResult;
}

bool UShopRunSubsystem::RequestNextDay_Implementation()
{
    if (!ReadyForCommand()) return false;
    if (State.Phase != EGamePhase::NightEnd || State.LastSettledDay != State.Day ||
        (Catalog.Rules.bEnableMarket && State.Day % Catalog.Rules.DaysPerWeek == 0 && State.LastMarketDay != State.Day))
    { Reject(EShopActionResult::InvalidPhase, TEXT("请先完成夜间结算和当晚黑市。")); return false; }
    FShopRunState Next = State;
    if (Next.Day >= Catalog.Rules.MaxDays) CheckEnding(Next, true);
    else
    {
        ++Next.Day; FText Error;
        if (!StartDay(Next, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return false; }
    }
    Commit(MoveTemp(Next)); return LastResult.bSucceeded;
}

void UShopRunSubsystem::CheckEnding(FShopRunState& Next, bool bFinal) const
{
    const FRunRules& R = Catalog.Rules;
    if (Next.Ending != EShopEnding::None) return;
    if (!Catalog.Endings.IsEmpty())
    {
        TArray<FEndingData> Rows; Catalog.Endings.GenerateValueArray(Rows);
        Rows.Sort([](const FEndingData& A, const FEndingData& B) { return A.Priority < B.Priority; });
        for (const FEndingData& Row : Rows)
        {
            bool bMatch = false;
            switch (Row.Condition)
            {
            case EShopEndingCondition::NegativeBalance:
                bMatch = (R.bImmediateBankruptcy && Next.Money < 0) || Next.NegativeDays >= Row.NegativeDaysRequired; break;
            case EShopEndingCondition::PollutionLimit:
                bMatch = Next.bPollutionLimitReached || Next.Pollution >= Row.PollutionThreshold; break;
            case EShopEndingCondition::FinalThresholds:
                bMatch = bFinal && Next.Enlighten >= Row.MinEnlighten && Next.Pollution < Row.MaxPollutionExclusive && (!Row.bRequireMoney || Next.Money >= Row.MinMoney); break;
            case EShopEndingCondition::FinalFallback: bMatch = bFinal; break;
            }
            if (bMatch) { Next.Ending = Row.Ending; break; }
        }
    }
    else
    {
        // Compatibility for the older prototype fixtures, which have no ending table.
        if ((R.bImmediateBankruptcy && Next.Money < 0) || Next.NegativeDays >= R.NegativeDaysToClose) Next.Ending = EShopEnding::Closed;
        else if (Next.bPollutionLimitReached || Next.Pollution >= R.PollutionLimit) Next.Ending = EShopEnding::PollutionReleased;
        else if (bFinal)
            Next.Ending = Next.Enlighten >= R.EnlightenWin && Next.Pollution < R.WinPollutionThreshold && (!R.bReturnRequiresRedeemTarget || Next.Money >= R.RedeemTarget)
                ? EShopEnding::Returned : EShopEnding::Cycle;
    }
    if (Next.Ending != EShopEnding::None)
    { Next.Phase = EGamePhase::End; Next.bPendingCalm = false; Next.PendingEvents.Reset(); Next.PendingEventId = NAME_None; Next.ActiveCustomer = INDEX_NONE; }
}

void UShopRunSubsystem::DispatchModal(FShopRunState& Next)
{
    if (Next.Phase == EGamePhase::End || ModalPhase(Next.Phase)) return;
    if (Next.bPendingCalm)
    { Next.ResumePhase = Next.Phase; Next.Phase = EGamePhase::Calm; Next.bPendingCalm = false; ShopDecrees::DrawCandidates(Next, Catalog); }
    else if (Catalog.Rules.bEnableHistory && !Next.PendingEvents.IsEmpty())
    { Next.ResumePhase = Next.Phase; Next.Phase = EGamePhase::History; Next.PendingEventId = Next.PendingEvents[0]; Next.PendingEvents.RemoveAt(0); }
}

void UShopRunSubsystem::Commit(FShopRunState&& Next, EShopActionResult Code, FName Id, int32 ResolvedCustomer, bool bNewRun)
{
    const EPollutionStage OldStage = ShopDecrees::GetStage(State.Pollution, Catalog.Rules);
    const EPollutionStage PeakStage = ShopDecrees::GetStage(FMath::Max(Next.Pollution, Next.PeakPollutionThisCommand), Catalog.Rules);
    const bool bStageRise = PeakStage > OldStage;
    FText Error;
    // One user action can advance at most one stage point, even if several thresholds
    // were crossed. Loophole effects in this same transaction cannot recursively tick.
    if (bStageRise || (bNewRun && PeakStage != EPollutionStage::Safe))
    {
        if (!bNewRun && Catalog.Rules.bAdvanceTurnOnStageRise)
        {
            TArray<FName> Triggered;
            if (!ShopDecrees::TickTurn(Next, Catalog, Triggered, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return; }
        }
        Next.bPendingCalm = true;
    }
    if (Next.StageResetPending)
    {
        // Tide resets the current alert/grace latch to the post-reduction stage. It
        // never changes the numeric thresholds or clears a reached terminal limit.
        Next.HeavyDueTurn = INDEX_NONE;
        Next.bGoldDuringHeavyGrace = false;
        Next.StageResetPending = false;
    }
    if (!ResolveHeavyWindow(Next, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return; }
    const EPollutionStage NewStage = ShopDecrees::GetStage(Next.Pollution, Catalog.Rules);
    if (NewStage == EPollutionStage::Heavy && Next.HeavyDueTurn == INDEX_NONE)
    { Next.HeavyDueTurn = Next.Turn + Catalog.Rules.HeavyGraceTurns; Next.bGoldDuringHeavyGrace = false; }
    if (NewStage != EPollutionStage::Heavy) { Next.HeavyDueTurn = INDEX_NONE; Next.bGoldDuringHeavyGrace = false; }
    Next.PeakPollutionThisCommand = 0;
    CheckEnding(Next);
    InjectPendingFakeCustomers(Next);
    DispatchModal(Next);
    FShopRunState Before = MoveTemp(State);
    State = MoveTemp(Next);
    const bool Success = Code == EShopActionResult::Success || Code == EShopActionResult::Opened || Code == EShopActionResult::Sold || Code == EShopActionResult::Cancelled;
    Result(Success, Code, FText(), Id);
    TGuardValue<bool> Guard(bCommitting, true);
    Notify(Before, ResolvedCustomer, bNewRun);
}

void UShopRunSubsystem::InjectPendingFakeCustomers(FShopRunState& Next)
{
    const bool bInside = Next.Phase == EGamePhase::Inside || Next.Phase == EGamePhase::InsideSell ||
        (ModalPhase(Next.Phase) && (Next.ResumePhase == EGamePhase::Inside || Next.ResumePhase == EGamePhase::InsideSell));
    if (!bInside || Next.PendingFakeCustomers <= 0) return;
    TArray<FName> Ids; Catalog.Customers.GetKeys(Ids); Ids.Sort(FNameLexicalLess());
    FName Template;
    for (FName Id : Ids) if (Catalog.Customers[Id].Kind == ECustomerKind::Normal) { Template = Id; break; }
    int32 Position = ShopCustomers::Current(Next);
    Position = Position == INDEX_NONE ? Next.Customers.Num() : Position + 1;
    const int32 Count = FMath::Min(Next.PendingFakeCustomers, 100);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FCustomerRuntime Fake;
        Fake.TemplateId = Template; Fake.Kind = ECustomerKind::Normal; Fake.NeedLayer = EBookLayer::Inside;
        Fake.NeedType = EBookType::Secret; Fake.bFake = true; Fake.bPolluted = true;
        Fake.MaxPatience = Fake.Patience = static_cast<float>(FMath::Max(1, Catalog.Rules.FakeCustomerMaxPollution));
        Next.Customers.Insert(Fake, Position++);
    }
    Next.PendingFakeCustomers -= Count;
}

void UShopRunSubsystem::Notify(const FShopRunState& Before, int32 ResolvedCustomer, bool bNewRun)
{
    if (bNewRun || Before.Day != State.Day) OnDayChanged.Broadcast();
    if (bNewRun || Before.Phase != State.Phase) OnPhaseChanged.Broadcast();
    if (bNewRun || Before.Money != State.Money) OnMoneyChanged.Broadcast();
    if (bNewRun || Before.Psychic != State.Psychic) OnPsychicChanged.Broadcast();
    if (bNewRun || Before.Pollution != State.Pollution) OnPollutionChanged.Broadcast();
    if (bNewRun || Before.Enlighten != State.Enlighten) OnEnlightenChanged.Broadcast();
    if (bNewRun || Before.Clues != State.Clues) OnCluesChanged.Broadcast();
    if (bNewRun || Before.Day != State.Day || Before.Turn != State.Turn || Catalog.Decrees.Contains(LastResult.SubjectId)) OnDecreeChanged.Broadcast();
    if (Before.MarketStock != State.MarketStock || Before.MarketSold != State.MarketSold || State.Phase == EGamePhase::Market) OnMarketChanged.Broadcast();
    OnInventoryChanged.Broadcast();
    OnCustomersChanged.Broadcast();
    if ((bNewRun || Before.Phase != EGamePhase::Calm) && State.Phase == EGamePhase::Calm) OnCalmOpened.Broadcast();
    if (ResolvedCustomer != INDEX_NONE && State.Customers.IsValidIndex(ResolvedCustomer))
        OnCustomerResolved.Broadcast(ResolvedCustomer, State.Customers[ResolvedCustomer].Resolution);
    const FRunSnapshot Snapshot = GetSnapshot_Implementation();
    const auto Receivers = Views;
    for (const auto& View : Receivers)
    {
        UObject* Target = View.Get(); if (!IsValid(Target)) continue;
        IShopView::Execute_RefreshShop(Target, Snapshot);
        if (bNewRun || Before.Day != State.Day) IShopView::Execute_NewDay(Target, State.Day);
        if (ResolvedCustomer != INDEX_NONE && State.Customers.IsValidIndex(ResolvedCustomer))
            IShopView::Execute_ResolveCustomer(Target, ResolvedCustomer, State.Customers[ResolvedCustomer].Resolution);
        if (State.Phase == EGamePhase::Calm && Before.Phase != EGamePhase::Calm) IShopView::Execute_OpenCalmPanel(Target, State.DecreeCandidates, Snapshot);
        if (State.Phase == EGamePhase::History && (Before.Phase != EGamePhase::History || Before.PendingEventId != State.PendingEventId))
            if (const FEventData* Event = Catalog.Events.Find(State.PendingEventId)) IShopView::Execute_ShowHistoryPanel(Target, State.PendingEventId, *Event);
        if (State.Phase == EGamePhase::Market && Before.Phase != EGamePhase::Market) IShopView::Execute_OpenMarket(Target, State.MarketStock, Snapshot);
        if (State.Phase == EGamePhase::End && Before.Phase != EGamePhase::End) IShopView::Execute_ShowEnding(Target, State.Ending, Snapshot);
    }
}

bool UShopRunSubsystem::RegisterView(UObject* View)
{
    if (!IsValid(View) || !View->GetClass()->ImplementsInterface(UShopView::StaticClass()) || bCommitting) return false;
    Views.AddUnique(TWeakObjectPtr<UObject>(View));
    TGuardValue<bool> Guard(bCommitting, true);
    const FRunSnapshot Snapshot = GetSnapshot_Implementation();
    IShopView::Execute_RefreshShop(View, Snapshot);
    if (State.Phase == EGamePhase::Calm) IShopView::Execute_OpenCalmPanel(View, State.DecreeCandidates, Snapshot);
    if (State.Phase == EGamePhase::History)
        if (const FEventData* Event = Catalog.Events.Find(State.PendingEventId)) IShopView::Execute_ShowHistoryPanel(View, State.PendingEventId, *Event);
    if (State.Phase == EGamePhase::Market) IShopView::Execute_OpenMarket(View, State.MarketStock, Snapshot);
    if (State.Phase == EGamePhase::End) IShopView::Execute_ShowEnding(View, State.Ending, Snapshot);
    return true;
}
void UShopRunSubsystem::UnregisterView(UObject* View) { Views.RemoveAll([View](const TWeakObjectPtr<UObject>& Entry) { return !Entry.IsValid() || Entry.Get() == View; }); }
bool UShopRunSubsystem::IsTickable() const { return !IsTemplate() && bConfigured && !bCommitting && (TradingPhase(State.Phase) || SellingPhase(State.Phase)); }
UWorld* UShopRunSubsystem::GetTickableGameObjectWorld() const { return GetWorld(); }
TStatId UShopRunSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UShopRunSubsystem, STATGROUP_Tickables); }
void UShopRunSubsystem::Tick(float DeltaTime)
{
    if (!IsTickable() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
    FShopRunState Next = State;
    const int32 Current = ShopCustomers::Current(Next);
    if (Current == INDEX_NONE) return;
    FCustomerRuntime& Customer = Next.Customers[Current];
    if (Customer.bFake)
    {
        const float Speed = Next.Pollution >= Catalog.Rules.MediumThreshold ? 1.f + Catalog.Rules.PatienceDropRatePolluted : 1.f;
        Customer.FakePollutionElapsed += FMath::Min(DeltaTime, Customer.Patience / Speed);
        const double Amount = FMath::FloorToDouble(Customer.FakePollutionElapsed) * Catalog.Rules.FakeCustomerPollutionPerSecond;
        const int32 Total = static_cast<int32>(FMath::Min(Amount, static_cast<double>(Catalog.Rules.FakeCustomerMaxPollution)));
        const int32 Delta = FMath::Max(0, Total - Customer.FakePollutionApplied);
        FText Error;
        if (!ShopEffects::ChangePollution(Next, Catalog, Delta, Error)) { Result(false, EShopActionResult::InvalidConfig, Error); return; }
        Customer.FakePollutionApplied = Total;
    }
    const int32 Expired = ShopCustomers::AdvancePatience(Next, Catalog.Rules, DeltaTime);
    if (Expired != INDEX_NONE) CompleteCustomer(Next, Expired, EShopActionResult::Expired);
    Commit(MoveTemp(Next), EShopActionResult::Success, NAME_None, Expired);
}
