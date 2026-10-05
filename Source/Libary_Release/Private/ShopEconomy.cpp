#include "ShopEconomy.h"

#include <cmath>

#define LOCTEXT_NAMESPACE "ShopEconomy"

namespace
{
    bool Fail(FText& Error, const FText& Message)
    {
        Error = Message;
        return false;
    }

    bool FitsInteger(int64 Value)
    {
        return Value >= MIN_int32 && Value <= MAX_int32;
    }

    bool IsInsideBook(const FBookData& Book)
    {
        return Book.Layer == EBookLayer::Inside && Book.BookType == EBookType::Secret;
    }

    bool IsTableBook(const FBookData& Book)
    {
        return Book.Layer == EBookLayer::Table &&
            (Book.BookType == EBookType::Novel || Book.BookType == EBookType::Poem || Book.BookType == EBookType::History);
    }

    bool ValidInventory(const FBookRuntime& Book)
    {
        return Book.Stock >= 0 && Book.AvailableToCollect >= 0 && Book.ReadCopies >= 0 && Book.ReadCopies <= Book.Stock;
    }

    bool CanAddStock(const FShopRunState& State)
    {
        int64 Count = 0;
        for (const auto& Pair : State.Inventory)
        {
            if (!ValidInventory(Pair.Value)) return false;
            Count += Pair.Value.Stock;
            if (Count >= MAX_int32) return false;
        }
        return true;
    }

    bool ScaledGain(const FShopRunState& State, EShopEffectType Type, int32 Base, int32& Result, FText& Error)
    {
        if (Base < 0) return Fail(Error, LOCTEXT("NegativeGain", "收益配置不能为负数。"));
        double Multiplier = 1.0;
        for (const FShopModifier& Modifier : State.Modifiers)
        {
            if (Modifier.Type != Type || (Modifier.EndTurn >= 0 && State.Turn >= Modifier.EndTurn)) continue;
            if (!std::isfinite(Modifier.Multiplier) || Modifier.Multiplier < 0.f)
                return Fail(Error, LOCTEXT("InvalidMultiplier", "收益倍率无效。"));
            Multiplier *= static_cast<double>(Modifier.Multiplier);
            if (!std::isfinite(Multiplier)) return Fail(Error, LOCTEXT("MultiplierOverflow", "收益倍率超出数值范围。"));
        }
        const double Scaled = std::floor(static_cast<double>(Base) * Multiplier);
        if (!std::isfinite(Scaled) || Scaled < 0.0 || Scaled > MAX_int32)
            return Fail(Error, LOCTEXT("GainOverflow", "收益超出整数范围。"));
        Result = static_cast<int32>(Scaled);
        return true;
    }

    bool PollutionAfter(const FShopRunState& State, const FRunRules& Rules, int64 Delta, int32& Result, FText& Error)
    {
        if (Rules.PollutionLimit <= 0 || Delta < 0 || State.Pollution < 0)
            return Fail(Error, LOCTEXT("InvalidPollution", "污染数值或上限配置无效。"));
        const int64 Sum = static_cast<int64>(State.Pollution) + Delta;
        if (!FitsInteger(Sum)) return Fail(Error, LOCTEXT("PollutionOverflow", "污染数值超出整数范围。"));
        Result = static_cast<int32>(Sum);
        return true;
    }

    void CommitPollution(FShopRunState& State, const FRunRules& Rules, int32 Pollution)
    {
        State.Pollution = Pollution;
        State.bPollutionLimitReached |= Pollution >= Rules.PollutionLimit;
    }

    int32 BookPollution(const FBookData& Book, int32 GlobalValue)
    {
        return Book.PollutionYield == -1 ? GlobalValue : Book.PollutionYield;
    }
}

void ShopEconomy::Reset(FShopRunState& State, const FShopCatalog& Catalog)
{
    State = FShopRunState();
    State.Money = Catalog.Rules.StartMoney;
    State.Psychic = Catalog.Rules.StartPsychic;
    State.Pollution = Catalog.Rules.StartPollution;
    State.Enlighten = Catalog.Rules.StartEnlighten;
    State.bPollutionLimitReached = State.Pollution >= Catalog.Rules.PollutionLimit;
    for (const auto& Pair : Catalog.Books)
    {
        FBookRuntime Runtime;
        if (Pair.Value.Layer == EBookLayer::Inside)
        {
            Runtime.Stock = Pair.Value.InitialOwnedStock;
            Runtime.AvailableToCollect = Pair.Value.InitialStock;
        }
        else Runtime.Stock = Pair.Value.InitialStock;
        State.Inventory.Add(Pair.Key, Runtime);
    }
}

bool ShopEconomy::AddMoney(FShopRunState& State, int32 Delta, FText& Error, bool bRequireFunds)
{
    Error = FText::GetEmpty();
    const int64 NewMoney = static_cast<int64>(State.Money) + Delta;
    const int64 NewIncome = static_cast<int64>(State.TodayIncome) + (Delta > 0 ? static_cast<int64>(Delta) : 0);
    const int64 NewExpense = static_cast<int64>(State.TodayExpense) + (Delta < 0 ? -static_cast<int64>(Delta) : 0);
    if (!FitsInteger(NewMoney) || !FitsInteger(NewIncome) || !FitsInteger(NewExpense))
        return Fail(Error, LOCTEXT("MoneyOverflow", "资金或当日账目超出整数范围。"));
    if (bRequireFunds && Delta < 0 && NewMoney < 0)
        return Fail(Error, LOCTEXT("InsufficientMoney", "资金不足。"));
    State.Money = static_cast<int32>(NewMoney);
    State.TodayIncome = static_cast<int32>(NewIncome);
    State.TodayExpense = static_cast<int32>(NewExpense);
    return true;
}

bool ShopEconomy::Restock(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return Fail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsTableBook(*Book) || Book->Cost < 0 || !ValidInventory(*Runtime))
        return Fail(Error, LOCTEXT("CannotRestock", "只有有效的表世界书籍可以补货。"));
    if (!CanAddStock(State)) return Fail(Error, LOCTEXT("StockOverflow", "库存超出整数范围。"));
    if (!AddMoney(State, -Book->Cost, Error, true)) return false;
    ++Runtime->Stock;
    return true;
}

bool ShopEconomy::Collect(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return Fail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsInsideBook(*Book) || !ValidInventory(*Runtime))
        return Fail(Error, LOCTEXT("CannotCollect", "只有有效的里世界密文书可以收取。"));
    if (Runtime->AvailableToCollect <= 0) return Fail(Error, LOCTEXT("NoOffer", "本晚已没有这本书可供收取。"));
    if (Catalog.Rules.CollectPsychicCost < 0 || State.Psychic < Catalog.Rules.CollectPsychicCost)
        return Fail(Error, LOCTEXT("InsufficientPsychic", "灵力不足或收书消耗配置无效。"));
    if (!CanAddStock(State)) return Fail(Error, LOCTEXT("StockOverflow", "库存超出整数范围。"));
    int32 NewPollution;
    if (!PollutionAfter(State, Catalog.Rules, Catalog.Rules.PollutionOnCollect, NewPollution, Error)) return false;
    State.Psychic -= Catalog.Rules.CollectPsychicCost;
    ++Runtime->Stock;
    --Runtime->AvailableToCollect;
    CommitPollution(State, Catalog.Rules, NewPollution);
    return true;
}

bool ShopEconomy::Read(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return Fail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsInsideBook(*Book) || !ValidInventory(*Runtime))
        return Fail(Error, LOCTEXT("CannotRead", "只有有效的里世界密文书可以阅读。"));
    if (Runtime->Stock <= 0) return Fail(Error, LOCTEXT("NoOwnedBook", "库存中没有这本书。"));
    if (!Catalog.Rules.bAllowRepeatRead && Runtime->ReadCopies >= Runtime->Stock)
        return Fail(Error, LOCTEXT("AlreadyRead", "已读完库存中的每一本书。"));
    int32 PsychicGain;
    const int32 BaseGain = Book->PsychicYield == -1 ? Catalog.Rules.ReadPsychicGain : Book->PsychicYield;
    if (!ScaledGain(State, EShopEffectType::PsychicGainMultiplier, BaseGain, PsychicGain, Error)) return false;
    const int64 NewPsychic = static_cast<int64>(State.Psychic) + PsychicGain;
    const int64 NewEnlighten = static_cast<int64>(State.Enlighten) + Book->EnlightenYield;
    if (State.Psychic < 0 || State.Enlighten < 0 || Book->EnlightenYield < 0 || !FitsInteger(NewPsychic) || !FitsInteger(NewEnlighten))
        return Fail(Error, LOCTEXT("ReadOverflow", "阅读收益配置无效或超出整数范围。"));
    int32 NewPollution;
    if (!PollutionAfter(State, Catalog.Rules, BookPollution(*Book, Catalog.Rules.PollutionOnRead), NewPollution, Error)) return false;
    if (!std::isfinite(Catalog.Rules.ClueDropChance) || Catalog.Rules.ClueDropChance < 0.f || Catalog.Rules.ClueDropChance > 1.f)
        return Fail(Error, LOCTEXT("InvalidClueChance", "线索掉落概率必须在 0 到 1 之间。"));

    State.Psychic = static_cast<int32>(NewPsychic);
    State.Enlighten = static_cast<int32>(NewEnlighten);
    if (Runtime->ReadCopies < Runtime->Stock) ++Runtime->ReadCopies;
    CommitPollution(State, Catalog.Rules, NewPollution);
    if (!Book->CluePool.IsEmpty() && Catalog.Rules.ClueDropChance > 0.f && State.Random.FRand() < Catalog.Rules.ClueDropChance)
    {
        TArray<FName> Clues = Book->CluePool;
        Clues.Remove(NAME_None);
        Clues.Sort(FNameLexicalLess());
        if (!Clues.IsEmpty()) State.Clues.AddUnique(Clues[State.Random.RandRange(0, Clues.Num() - 1)]);
    }
    return true;
}

EShopActionResult ShopEconomy::Sell(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, const FCustomerRuntime& Customer, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime)
    {
        Fail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
        return EShopActionResult::InvalidId;
    }
    const bool bInsideCustomer = Customer.NeedLayer == EBookLayer::Inside && Customer.Kind == ECustomerKind::Secret && Customer.NeedType == EBookType::Secret;
    const bool bTableCustomer = Customer.NeedLayer == EBookLayer::Table &&
        (Customer.Kind == ECustomerKind::Normal || Customer.Kind == ECustomerKind::Hurry || Customer.Kind == ECustomerKind::Polluted) &&
        (Customer.NeedType == EBookType::Novel || Customer.NeedType == EBookType::Poem || Customer.NeedType == EBookType::History);
    if (Customer.bServed || (!bInsideCustomer && !bTableCustomer) || !ValidInventory(*Runtime))
    {
        Fail(Error, LOCTEXT("InvalidCustomer", "顾客已离开或交易数据无效。"));
        return EShopActionResult::Rejected;
    }
    if (Runtime->Stock <= 0)
    {
        Fail(Error, LOCTEXT("NoOwnedBook", "库存中没有这本书。"));
        return EShopActionResult::OutOfStock;
    }
    if (Book->BookType != Customer.NeedType || Book->Layer != Customer.NeedLayer)
    {
        Fail(Error, LOCTEXT("WrongBook", "这本书不符合顾客的需求。"));
        return EShopActionResult::WrongBook;
    }
    int32 Income = Book->Price;
    if (Book->Price < 0 || (bInsideCustomer && !ScaledGain(State, EShopEffectType::IncomeMultiplier, Book->Price, Income, Error)))
    {
        if (Error.IsEmpty()) Fail(Error, LOCTEXT("InvalidPrice", "售价配置无效。"));
        return EShopActionResult::InvalidConfig;
    }
    if (State.TotalSold < 0 || State.TotalSold == MAX_int32)
    {
        Fail(Error, LOCTEXT("SoldOverflow", "累计售出数量超出整数范围。"));
        return EShopActionResult::Rejected;
    }
    int64 PollutionDelta = bInsideCustomer ? Catalog.Rules.PollutionOnSell : 0;
    if (PollutionDelta < 0 || ((Customer.bPolluted || Customer.Kind == ECustomerKind::Polluted) && Catalog.Rules.PollutionOnPollutedCustomer < 0))
    {
        Fail(Error, LOCTEXT("InvalidPollution", "污染数值或上限配置无效。"));
        return EShopActionResult::InvalidConfig;
    }
    if (Customer.bPolluted || Customer.Kind == ECustomerKind::Polluted) PollutionDelta += Catalog.Rules.PollutionOnPollutedCustomer;
    int32 NewPollution;
    if (!PollutionAfter(State, Catalog.Rules, PollutionDelta, NewPollution, Error)) return EShopActionResult::Rejected;
    if (!AddMoney(State, Income, Error)) return EShopActionResult::Rejected;
    --Runtime->Stock;
    if (Runtime->ReadCopies > 0) --Runtime->ReadCopies;
    ++State.TotalSold;
    CommitPollution(State, Catalog.Rules, NewPollution);
    return EShopActionResult::Sold;
}

bool ShopEconomy::HasMatchingStock(const FShopRunState& State, const FShopCatalog& Catalog, EBookType Type, EBookLayer Layer)
{
    if ((Layer == EBookLayer::Inside && Type != EBookType::Secret) || (Layer == EBookLayer::Table && Type == EBookType::Secret)) return false;
    for (const auto& Pair : Catalog.Books)
    {
        if (Pair.Value.BookType != Type || Pair.Value.Layer != Layer) continue;
        const FBookRuntime* Runtime = State.Inventory.Find(Pair.Key);
        if (Runtime && ValidInventory(*Runtime) && Runtime->Stock > 0) return true;
    }
    return false;
}

bool ShopEconomy::PayRent(FShopRunState& State, const FRunRules& Rules, FText& Error)
{
    Error = FText::GetEmpty();
    if (State.Day <= 0 || Rules.Rent < 0) return Fail(Error, LOCTEXT("InvalidRent", "房租或营业日配置无效。"));
    if (State.LastRentDay >= State.Day) return Fail(Error, LOCTEXT("RentAlreadyPaid", "今天的房租已结算。"));
    if (!AddMoney(State, -Rules.Rent, Error)) return false;
    State.LastRentDay = State.Day;
    return true;
}

int32 ShopEconomy::TotalStock(const FShopRunState& State)
{
    int64 Result = 0;
    for (const auto& Pair : State.Inventory)
    {
        Result += FMath::Max(0, Pair.Value.Stock);
        if (Result >= MAX_int32) return MAX_int32;
    }
    return static_cast<int32>(Result);
}

#undef LOCTEXT_NAMESPACE
