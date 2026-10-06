#include "ShopEconomy.h"
#include "ShopDecrees.h"

#include <cmath>

#define LOCTEXT_NAMESPACE "ShopEconomy"

namespace
{
    bool EconomyFail(FText& Error, const FText& Message)
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

    bool ScaledGain(const FShopRunState& State, EShopEffectType Type, int32 Base, int32& Result, FText& Error, bool bNightIncome = false)
    {
        if (Base < 0) return EconomyFail(Error, LOCTEXT("NegativeGain", "收益配置不能为负数。"));
        double Multiplier = 1.0;
        for (const FShopModifier& Modifier : State.Modifiers)
        {
            if ((Modifier.Type != Type && !(bNightIncome && Modifier.Type == EShopEffectType::NightIncomeMultiplier)) ||
                (Modifier.EndTurn >= 0 && State.Turn >= Modifier.EndTurn)) continue;
            if (!std::isfinite(Modifier.Multiplier) || Modifier.Multiplier < 0.f)
                return EconomyFail(Error, LOCTEXT("InvalidMultiplier", "收益倍率无效。"));
            Multiplier *= static_cast<double>(Modifier.Multiplier);
            if (!std::isfinite(Multiplier)) return EconomyFail(Error, LOCTEXT("MultiplierOverflow", "收益倍率超出数值范围。"));
        }
        const double Scaled = std::floor(static_cast<double>(Base) * Multiplier);
        if (!std::isfinite(Scaled) || Scaled < 0.0 || Scaled > MAX_int32)
            return EconomyFail(Error, LOCTEXT("GainOverflow", "收益超出整数范围。"));
        Result = static_cast<int32>(Scaled);
        return true;
    }

    bool AddPollution(FShopRunState& State, const FShopCatalog& Catalog, int64 Delta, FText& Error)
    {
        if (Delta < 0 || !FitsInteger(Delta)) return EconomyFail(Error, LOCTEXT("InvalidPollution", "污染增量无效或超出整数范围。"));
        return ShopEffects::ChangePollution(State, Catalog, static_cast<int32>(Delta), Error);
    }

    int32 BookPollution(const FBookData& Book, int32 GlobalValue)
    {
        return Book.PollutionYield == -1 ? GlobalValue : Book.PollutionYield;
    }
}

bool ShopEconomy::IsSecretInventoryValid(const FBookRuntime& Book)
{
    if (!ValidInventory(Book) || Book.Stock != Book.SecretCopies.Num()) return false;
    int32 Read = 0;
    int32 Listed = 0;
    bool bAltered = false;
    for (const FSecretBookCopy& Copy : Book.SecretCopies)
    {
        if (Copy.bRead) ++Read;
        if (Copy.bListedForSale) ++Listed;
        bAltered |= Copy.bAltered;
    }
    return Book.ReadCopies == Read && Book.ListedCopies == Listed &&
        Book.StoredCopies == Book.Stock - Listed && Book.bAltered == bAltered;
}

void ShopEconomy::RefreshBookCounts(FBookRuntime& Book)
{
    Book.Stock = Book.SecretCopies.Num();
    Book.ReadCopies = 0;
    Book.ListedCopies = 0;
    Book.StoredCopies = 0;
    Book.bAltered = false;
    for (const FSecretBookCopy& Copy : Book.SecretCopies)
    {
        if (Copy.bRead) ++Book.ReadCopies;
        if (Copy.bListedForSale) ++Book.ListedCopies;
        else ++Book.StoredCopies;
        Book.bAltered |= Copy.bAltered;
    }
}

void ShopEconomy::NormalizeSecretCopies(FBookRuntime& Book)
{
    // Explicit compatibility conversion for old fixtures, never an implicit live-state repair.
    // Once a copy list exists, aggregate fields never overwrite the individual copy flags.
    if (Book.SecretCopies.IsEmpty() && Book.Stock > 0)
    {
        Book.SecretCopies.SetNum(Book.Stock);
        for (int32 Index = 0; Index < Book.SecretCopies.Num(); ++Index)
        {
            FSecretBookCopy& Copy = Book.SecretCopies[Index];
            Copy.bRead = Index < Book.ReadCopies;
            Copy.bSealed = true;
            Copy.bAltered = Book.bAltered;
            Copy.bListedForSale = false;
        }
    }
    RefreshBookCounts(Book);
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
        Runtime.Stock = Pair.Value.InitialStock;
        if (Pair.Value.Layer == EBookLayer::Inside)
        {
            Runtime.AvailableToCollect = Pair.Value.CollectOfferPerNight;
            // The validated catalog explicitly grants these new, unlisted copies at run start.
            Runtime.SecretCopies.SetNum(Runtime.Stock);
            RefreshBookCounts(Runtime);
        }
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
        return EconomyFail(Error, LOCTEXT("MoneyOverflow", "资金或当日账目超出整数范围。"));
    if (bRequireFunds && Delta < 0 && NewMoney < 0)
        return EconomyFail(Error, LOCTEXT("InsufficientMoney", "资金不足。"));
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
    if (!Book || !Runtime) return EconomyFail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsTableBook(*Book) || Book->Cost < 0 || !ValidInventory(*Runtime))
        return EconomyFail(Error, LOCTEXT("CannotRestock", "只有有效的表世界书籍可以补货。"));
    if (!CanAddStock(State)) return EconomyFail(Error, LOCTEXT("StockOverflow", "库存超出整数范围。"));
    if (!AddMoney(State, -Book->Cost, Error, true)) return false;
    ++Runtime->Stock;
    return true;
}

bool ShopEconomy::Collect(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return EconomyFail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsInsideBook(*Book) || !IsSecretInventoryValid(*Runtime))
        return EconomyFail(Error, LOCTEXT("CannotCollect", "只有有效的里世界密文书可以收取。"));
    if (Runtime->AvailableToCollect <= 0) return EconomyFail(Error, LOCTEXT("NoOffer", "本晚已没有这本书可供收取。"));
    if (Catalog.Rules.CollectPsychicCost < 0 || State.Psychic < Catalog.Rules.CollectPsychicCost)
        return EconomyFail(Error, LOCTEXT("InsufficientPsychic", "灵力不足或收书消耗配置无效。"));
    if (!CanAddStock(State)) return EconomyFail(Error, LOCTEXT("StockOverflow", "库存超出整数范围。"));
    FShopRunState Next = State;
    FBookRuntime& NextBook = Next.Inventory.FindChecked(BookId);
    if (!AddPollution(Next, Catalog, Catalog.Rules.PollutionOnCollect, Error)) return false;
    Next.Psychic -= Catalog.Rules.CollectPsychicCost;
    FSecretBookCopy Copy;
    Copy.bSealed = true;
    Copy.bListedForSale = false;
    NextBook.SecretCopies.Add(Copy);
    --NextBook.AvailableToCollect;
    RefreshBookCounts(NextBook);
    State = MoveTemp(Next);
    return true;
}

bool ShopEconomy::Read(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return EconomyFail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsInsideBook(*Book) || !IsSecretInventoryValid(*Runtime))
        return EconomyFail(Error, LOCTEXT("CannotRead", "只有有效的里世界密文书可以阅读。"));
    if (Runtime->Stock <= 0) return EconomyFail(Error, LOCTEXT("NoOwnedBook", "库存中没有这本书。"));
    if (!Catalog.Rules.bAllowRepeatRead && Runtime->ReadCopies >= Runtime->Stock)
        return EconomyFail(Error, LOCTEXT("AlreadyRead", "已读完库存中的每一本书。"));
    FShopRunState Next = State;
    FBookRuntime& NextBook = Next.Inventory.FindChecked(BookId);
    int32 CopyIndex = NextBook.SecretCopies.IndexOfByPredicate([](const FSecretBookCopy& Copy) { return !Copy.bRead; });
    if (CopyIndex == INDEX_NONE && Catalog.Rules.bAllowRepeatRead && !NextBook.SecretCopies.IsEmpty()) CopyIndex = 0;
    if (CopyIndex == INDEX_NONE) return EconomyFail(Error, LOCTEXT("AlreadyRead", "已读完库存中的每一本书。"));
    FSecretBookCopy& Copy = NextBook.SecretCopies[CopyIndex];
    int32 PsychicGain;
    const int32 BaseGain = Book->PsychicYield == -1 ? Catalog.Rules.ReadPsychicGain : Book->PsychicYield;
    if (!ScaledGain(State, EShopEffectType::PsychicGainMultiplier, BaseGain, PsychicGain, Error)) return false;
    const int64 NewPsychic = FMath::Min<int64>(Catalog.Rules.PsychicMax, static_cast<int64>(State.Psychic) + PsychicGain);
    const int64 NewEnlighten = static_cast<int64>(State.Enlighten) + Book->EnlightenYield;
    if (Catalog.Rules.PsychicMax <= 0 || State.Psychic < 0 || State.Enlighten < 0 || Book->EnlightenYield < 0 || !FitsInteger(NewPsychic) || !FitsInteger(NewEnlighten))
        return EconomyFail(Error, LOCTEXT("ReadOverflow", "阅读收益配置无效或超出整数范围。"));
    const int32 BasePollution = BookPollution(*Book, Catalog.Rules.PollutionOnRead);
    if (BasePollution < 0 || Catalog.Rules.AlteredBookReadPollution < 0 || Catalog.Rules.ReturnedBookReadPollution < 0)
        return EconomyFail(Error, LOCTEXT("InvalidReadPollution", "阅读污染配置无效。"));
    const int64 Pollution = static_cast<int64>(BasePollution) +
        (Copy.bAltered ? Catalog.Rules.AlteredBookReadPollution : 0) + (Copy.bPolluted ? Catalog.Rules.ReturnedBookReadPollution : 0);
    if (!AddPollution(Next, Catalog, Pollution, Error)) return false;
    if (!std::isfinite(Catalog.Rules.ClueDropChance) || Catalog.Rules.ClueDropChance < 0.f || Catalog.Rules.ClueDropChance > 1.f)
        return EconomyFail(Error, LOCTEXT("InvalidClueChance", "线索掉落概率必须在 0 到 1 之间。"));

    Next.Psychic = static_cast<int32>(NewPsychic);
    Next.Enlighten = static_cast<int32>(NewEnlighten);
    Copy.bRead = true;
    RefreshBookCounts(NextBook);
    if (!Book->CluePool.IsEmpty() && Catalog.Rules.ClueDropChance > 0.f && Next.Random.FRand() < Catalog.Rules.ClueDropChance)
    {
        TArray<FName> Clues = Book->CluePool;
        Clues.Remove(NAME_None);
        Clues.Sort(FNameLexicalLess());
        if (!Clues.IsEmpty()) Next.Clues.AddUnique(Clues[Next.Random.RandRange(0, Clues.Num() - 1)]);
    }
    State = MoveTemp(Next);
    return true;
}

bool ShopEconomy::SetSecretListing(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, bool bListed, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    const FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime) return EconomyFail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
    if (!IsInsideBook(*Book) || !IsSecretInventoryValid(*Runtime))
        return EconomyFail(Error, LOCTEXT("CannotList", "只有有效的秘密书库存可以上架或下架。"));

    FShopRunState Next = State;
    FBookRuntime& NextBook = Next.Inventory.FindChecked(BookId);
    const int32 CopyIndex = NextBook.SecretCopies.IndexOfByPredicate([bListed](const FSecretBookCopy& Copy)
    {
        return Copy.bListedForSale != bListed;
    });
    if (CopyIndex == INDEX_NONE)
        return EconomyFail(Error, bListed
            ? LOCTEXT("NoStoredCopy", "没有未上架的秘密书可供上架。")
            : LOCTEXT("NoListedCopy", "没有已上架的秘密书可供下架。"));
    NextBook.SecretCopies[CopyIndex].bListedForSale = bListed;
    RefreshBookCounts(NextBook);
    State = MoveTemp(Next);
    return true;
}

EShopActionResult ShopEconomy::Sell(FShopRunState& State, const FShopCatalog& Catalog, FName BookId, const FCustomerRuntime& Customer, FText& Error)
{
    Error = FText::GetEmpty();
    const FBookData* Book = Catalog.Books.Find(BookId);
    FBookRuntime* Runtime = State.Inventory.Find(BookId);
    if (!Book || !Runtime)
    {
        EconomyFail(Error, LOCTEXT("UnknownBook", "书籍不存在。"));
        return EShopActionResult::InvalidId;
    }
    const bool bSecretBook = IsInsideBook(*Book);
    if (bSecretBook && !IsSecretInventoryValid(*Runtime))
    {
        EconomyFail(Error, LOCTEXT("InvalidSecretInventory", "秘密书库存与副本记录不一致，无法交易。"));
        return EShopActionResult::Rejected;
    }
    // NeedLayer describes the requested book's origin; every customer trades in the surface shop.
    const bool bSecretCustomer = Customer.Kind == ECustomerKind::Secret &&
        Customer.NeedLayer == EBookLayer::Inside && Customer.NeedType == EBookType::Secret;
    const bool bTableCustomer = Customer.NeedLayer == EBookLayer::Table &&
        (Customer.Kind == ECustomerKind::Normal || Customer.Kind == ECustomerKind::Hurry || Customer.Kind == ECustomerKind::Polluted) &&
        (Customer.NeedType == EBookType::Novel || Customer.NeedType == EBookType::Poem || Customer.NeedType == EBookType::History);
    if (Customer.bServed || Customer.bFake || (!bSecretCustomer && !bTableCustomer) || !ValidInventory(*Runtime))
    {
        EconomyFail(Error, LOCTEXT("InvalidCustomer", "顾客已离开或交易数据无效。"));
        return EShopActionResult::Rejected;
    }
    if (Runtime->Stock <= 0)
    {
        EconomyFail(Error, LOCTEXT("NoOwnedBook", "库存中没有这本书。"));
        return EShopActionResult::OutOfStock;
    }
    if (Book->BookType != Customer.NeedType || Book->Layer != Customer.NeedLayer)
    {
        EconomyFail(Error, LOCTEXT("WrongBook", "这本书不符合顾客的需求。"));
        return EShopActionResult::WrongBook;
    }
    const bool bNightSale = State.Phase == EGamePhase::NightShop || State.Phase == EGamePhase::NightSell;
    const EShopEffectType IncomeType = bSecretBook ? EShopEffectType::IncomeMultiplier : EShopEffectType::NightIncomeMultiplier;
    int32 Income = Book->Price;
    if (Book->Price < 0 || ((bSecretBook || bNightSale) &&
        !ScaledGain(State, IncomeType, Book->Price, Income, Error, bSecretBook && bNightSale)))
    {
        if (Error.IsEmpty()) EconomyFail(Error, LOCTEXT("InvalidPrice", "售价配置无效。"));
        return EShopActionResult::InvalidConfig;
    }
    if (State.TotalSold < 0 || State.TotalSold == MAX_int32)
    {
        EconomyFail(Error, LOCTEXT("SoldOverflow", "累计售出数量超出整数范围。"));
        return EShopActionResult::Rejected;
    }
    if (!std::isfinite(Book->SaleEnlightenChance) || Book->SaleEnlightenChance < 0.f || Book->SaleEnlightenChance > 1.f || Book->SaleEnlightenYield < 0)
    {
        EconomyFail(Error, LOCTEXT("InvalidSaleEnlightenment", "售书启蒙收益或概率配置无效。"));
        return EShopActionResult::InvalidConfig;
    }
    FShopRunState Next = State;
    FBookRuntime& NextBook = Next.Inventory.FindChecked(BookId);
    int32 CopyIndex = INDEX_NONE;
    int64 PollutionDelta = bSecretBook ? Catalog.Rules.PollutionOnSell : 0;
    if (bSecretBook)
    {
        CopyIndex = NextBook.SecretCopies.IndexOfByPredicate([](const FSecretBookCopy& Copy) { return Copy.bListedForSale && Copy.bRead; });
        if (CopyIndex == INDEX_NONE)
            CopyIndex = NextBook.SecretCopies.IndexOfByPredicate([](const FSecretBookCopy& Copy) { return Copy.bListedForSale; });
        if (CopyIndex == INDEX_NONE)
        {
            EconomyFail(Error, LOCTEXT("NoListedCopyForSale", "这本秘密书尚未上架表店，无法出售。"));
            return EShopActionResult::OutOfStock;
        }
        if (Catalog.Rules.ReturnedBookSellPollution < 0 || Next.LostSecretBooks.Num() == MAX_int32)
        {
            EconomyFail(Error, LOCTEXT("InvalidReturnedBook", "返架书污染配置无效或流失记录已满。"));
            return EShopActionResult::Rejected;
        }
        if (NextBook.SecretCopies[CopyIndex].bPolluted) PollutionDelta += Catalog.Rules.ReturnedBookSellPollution;
    }
    if (PollutionDelta < 0 || ((Customer.bPolluted || Customer.Kind == ECustomerKind::Polluted) && Catalog.Rules.PollutionOnPollutedCustomer < 0))
    {
        EconomyFail(Error, LOCTEXT("InvalidPollution", "污染数值或上限配置无效。"));
        return EShopActionResult::InvalidConfig;
    }
    if (Customer.bPolluted || Customer.Kind == ECustomerKind::Polluted) PollutionDelta += Catalog.Rules.PollutionOnPollutedCustomer;
    if (!AddPollution(Next, Catalog, PollutionDelta, Error)) return EShopActionResult::Rejected;
    if (!AddMoney(Next, Income, Error)) return EShopActionResult::Rejected;
    if (Book->SaleEnlightenYield > 0 && Book->SaleEnlightenChance > 0.f &&
        (Book->SaleEnlightenChance >= 1.f || Next.Random.FRand() < Book->SaleEnlightenChance))
    {
        const int64 NewEnlighten = static_cast<int64>(Next.Enlighten) + Book->SaleEnlightenYield;
        if (Next.Enlighten < 0 || !FitsInteger(NewEnlighten))
        {
            EconomyFail(Error, LOCTEXT("EnlightenOverflow", "启蒙收益超出整数范围。"));
            return EShopActionResult::Rejected;
        }
        Next.Enlighten = static_cast<int32>(NewEnlighten);
    }
    if (bSecretBook)
    {
        NextBook.SecretCopies.RemoveAt(CopyIndex);
        RefreshBookCounts(NextBook);
        Next.LostSecretBooks.Add(BookId);
    }
    else --NextBook.Stock;
    ++Next.TotalSold;
    State = MoveTemp(Next);
    return EShopActionResult::Sold;
}

bool ShopEconomy::HasMatchingStock(const FShopRunState& State, const FShopCatalog& Catalog, EBookType Type, EBookLayer Layer)
{
    if ((Layer == EBookLayer::Inside && Type != EBookType::Secret) || (Layer == EBookLayer::Table && Type == EBookType::Secret)) return false;
    for (const auto& Pair : Catalog.Books)
    {
        if (Pair.Value.BookType != Type || Pair.Value.Layer != Layer) continue;
        const FBookRuntime* Runtime = State.Inventory.Find(Pair.Key);
        if (!Runtime || !ValidInventory(*Runtime) || Runtime->Stock <= 0) continue;
        if (Type != EBookType::Secret) return true;
        if (!IsSecretInventoryValid(*Runtime)) continue;
        if (Runtime->SecretCopies.ContainsByPredicate([](const FSecretBookCopy& Copy) { return Copy.bListedForSale; })) return true;
    }
    return false;
}

bool ShopEconomy::PayRent(FShopRunState& State, const FRunRules& Rules, FText& Error)
{
    Error = FText::GetEmpty();
    const int64 Rent = static_cast<int64>(Rules.Rent) + State.RentPenalty;
    if (State.Day <= 0 || Rules.Rent < 0 || State.RentPenalty < 0 || Rent < 0 || !FitsInteger(Rent))
        return EconomyFail(Error, LOCTEXT("InvalidRent", "房租或营业日配置无效。"));
    if (State.LastRentDay >= State.Day) return EconomyFail(Error, LOCTEXT("RentAlreadyPaid", "今天的房租已结算。"));
    if (!AddMoney(State, -static_cast<int32>(Rent), Error)) return false;
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
