#include "ShopStory.h"

#include "ShopDecrees.h"
#include "ShopEconomy.h"
#include <cmath>

#define LOCTEXT_NAMESPACE "ShopStory"

namespace
{
    bool Fail(FText& Error, const FText& Message)
    {
        Error = Message;
        return false;
    }

    bool HasEveryClue(const FShopRunState& State, const TArray<FName>& RequiredClues)
    {
        for (FName Clue : RequiredClues)
            if (Clue.IsNone() || !State.Clues.Contains(Clue)) return false;
        return true;
    }
}

void ShopStory::QueueEvents(FShopRunState& State, const FShopCatalog& Catalog, EShopEventTrigger Trigger, FName BookId)
{
    TArray<FName> Ids;
    Catalog.Events.GetKeys(Ids);
    Ids.Sort(FNameLexicalLess());
    if (Catalog.Rules.bUseHistoryFragments)
    {
        if (Trigger != EShopEventTrigger::OnRead) return;
        Ids.RemoveAll([&](FName Id)
        {
            const FEventData& Page = Catalog.Events.FindChecked(Id);
            return !Page.bEnabled || State.CollectedHistoryPages.Contains(Id) || State.PendingEventId == Id || State.PendingEvents.Contains(Id);
        });
        // A single 30% roll per successful read, then one uniformly selected uncollected page.
        // Never roll independently seven times, and never grant reading rewards again on closing.
        if (!Ids.IsEmpty() && State.Random.FRand() < Catalog.Rules.HistoryFragmentChance)
        {
            const FName Id = Ids[State.Random.RandRange(0, Ids.Num() - 1)];
            State.CollectedHistoryPages.Add(Id); State.PendingEvents.Add(Id);
        }
        return;
    }
    for (FName Id : Ids)
    {
        const FEventData& Event = Catalog.Events.FindChecked(Id);
        if (!Event.bEnabled || Event.Trigger != Trigger || State.Day < Event.MinDay || State.Day > Event.MaxDay) continue;
        if (!Event.RequiredBookId.IsNone() && Event.RequiredBookId != BookId) continue;
        if (!HasEveryClue(State, Event.RequiredClues)) continue;
        if (Event.bOncePerRun && State.WitnessedEvents.Contains(Id)) continue;
        if (State.PendingEventId == Id || State.PendingEvents.Contains(Id)) continue;
        if (!std::isfinite(Event.Chance) || Event.Chance <= 0.f || Event.Chance > 1.f) continue;
        if (Event.Chance < 1.f && State.Random.FRand() >= Event.Chance) continue;
        State.PendingEvents.Add(Id);
    }
}

bool ShopStory::Witness(FShopRunState& State, const FShopCatalog& Catalog, EHistoryChoice Choice, FText& Error)
{
    Error = FText::GetEmpty();
    if (Choice != EHistoryChoice::Witness) return Fail(Error, LOCTEXT("InvalidChoice", "当前历史事件只支持见证选项。"));
    if (State.PendingEventId.IsNone()) return Fail(Error, LOCTEXT("NoPendingEvent", "当前没有等待见证的历史事件。"));
    const FName Id = State.PendingEventId;
    const FEventData* Event = Catalog.Events.Find(Id);
    if (!Event || !Event->bEnabled) return Fail(Error, LOCTEXT("MissingEvent", "等待见证的历史事件数据不可用。"));
    if (Event->bOncePerRun && State.WitnessedEvents.Contains(Id))
        return Fail(Error, LOCTEXT("EventAlreadyWitnessed", "本局已见证过这一历史事件。"));
    if (Catalog.Rules.PollutionOnHistory < 0) return Fail(Error, LOCTEXT("InvalidHistoryPollution", "历史事件的污染增量配置无效。"));

    FShopRunState Next = State;
    if (!Catalog.Rules.bUseHistoryFragments && !ShopEffects::Apply(Next, Catalog, Event->ResultA, Id, -1, false, Error)) return false;
    FShopEffect Pollution;
    Pollution.Type = EShopEffectType::Pollution;
    Pollution.Amount = Catalog.Rules.bUseHistoryFragments ? 0 : Catalog.Rules.PollutionOnHistory;
    TArray<FShopEffect> HistoryEffects;
    HistoryEffects.Add(Pollution);
    if (!ShopEffects::Apply(Next, Catalog, HistoryEffects, Id, -1, false, Error)) return false;
    Next.WitnessedEvents.AddUnique(Id);
    Next.PendingEventId = NAME_None;
    Next.PendingEvents.Remove(Id);
    State = MoveTemp(Next);
    return true;
}

bool ShopStory::OpenMarket(FShopRunState& State, const FShopCatalog& Catalog, FText& Error)
{
    Error = FText::GetEmpty();
    const FRunRules& Rules = Catalog.Rules;
    if (!Rules.bEnableMarket) return Fail(Error, LOCTEXT("MarketDisabled", "当前配置未开放午夜集市。"));
    if (Rules.DaysPerWeek <= 0) return Fail(Error, LOCTEXT("InvalidWeekLength", "每周天数配置无效。"));
    const bool bCorrectPhase = Rules.bSecretBookMarket
        ? State.Phase == EGamePhase::DuskChoice && State.NightChoice == ENightChoice::None
        : State.Phase == EGamePhase::NightEnd && State.LastSettledDay == State.Day;
    if (!bCorrectPhase || State.Day <= 0 || State.Day % Rules.DaysPerWeek != 0)
        return Fail(Error, Rules.bSecretBookMarket
            ? LOCTEXT("SecretMarketNotOpen", "黑市只在每隔七天的夜晚选择阶段开放，本晚只能选择一项活动。")
            : LOCTEXT("MarketNotOpen", "午夜集市只在每周末的夜间结算完成后开放。"));
    if (State.LastMarketDay == State.Day) return Fail(Error, LOCTEXT("MarketAlreadyClosed", "本晚的午夜集市已经结束。"));
    const int32 Week = 1 + (State.Day - 1) / Rules.DaysPerWeek;
    TArray<FName> Items;
    for (const auto& Pair : Catalog.MarketItems)
    {
        const FMarketItemData& Item = Pair.Value;
        if (!Item.bEnabled || (Item.Week != 0 && Item.Week != Week)) continue;
        if (Item.bOnePerRun && State.PurchasedItems.Contains(Pair.Key)) continue;
        Items.Add(Pair.Key);
    }
    Items.Sort(FNameLexicalLess());
    State.MarketStock = MoveTemp(Items);
    State.MarketSold.Reset();
    State.Phase = EGamePhase::Market;
    if (Rules.bSecretBookMarket) State.NightChoice = ENightChoice::Market;
    // LastMarketDay is written when the coordinator closes the market.
    return true;
}

bool ShopStory::BuyMarketItem(FShopRunState& State, const FShopCatalog& Catalog, FName ItemId, FText& Error)
{
    Error = FText::GetEmpty();
    if (State.Phase != EGamePhase::Market) return Fail(Error, LOCTEXT("NotAtMarket", "当前不在午夜集市。"));
    const FMarketItemData* Item = Catalog.MarketItems.Find(ItemId);
    if (!Item || !Item->bEnabled || !State.MarketStock.Contains(ItemId))
        return Fail(Error, LOCTEXT("UnavailableItem", "本晚集市没有这件商品。"));
    if (State.MarketSold.Contains(ItemId) || (Item->bOnePerRun && State.PurchasedItems.Contains(ItemId)))
        return Fail(Error, LOCTEXT("ItemAlreadyPurchased", "这件商品已经购买过。"));
    if (Item->Price < 0) return Fail(Error, LOCTEXT("InvalidMarketPrice", "集市商品价格配置无效。"));

    FShopRunState Next = State;
    if (Catalog.Rules.bSecretBookMarket)
    {
        if (!ShopEconomy::BuySecret(Next, Catalog, Item->SecretBookId, Item->Price, Error)) return false;
        Next.MarketSold.AddUnique(ItemId);
        State = MoveTemp(Next);
        return true;
    }
    if (!ShopEconomy::AddMoney(Next, -Item->Price, Error, true)) return false;
    if (!ShopEffects::Apply(Next, Catalog, Item->TabooCost, ItemId, -1, true, Error)) return false;
    if (!ShopEffects::Apply(Next, Catalog, Item->Effect, ItemId, -1, false, Error)) return false;
    if (!ShopEffects::Apply(Next, Catalog, Item->SideEffect, ItemId, -1, false, Error)) return false;
    Next.MarketSold.AddUnique(ItemId);
    if (Item->bOnePerRun) Next.PurchasedItems.AddUnique(ItemId);
    State = MoveTemp(Next);
    return true;
}

bool ShopStory::OwlTalk(FShopRunState& State, const FShopCatalog& Catalog, FName& OutId, FText& OutText, FText& Error)
{
    OutId = NAME_None;
    OutText = FText::GetEmpty();
    Error = FText::GetEmpty();
    if (State.OwlTalkCount < 0) return Fail(Error, LOCTEXT("InvalidTalkCount", "夜枭对话进度无效。"));
    const int32 GuideIndex = State.OwlTalkCount < 10 ? State.OwlTalkCount + 1 : 0;
    TArray<FName> Ids;
    for (const auto& Pair : Catalog.OwlLines)
        if (Pair.Value.GuideIndex == GuideIndex) Ids.Add(Pair.Key);
    Ids.Sort(FNameLexicalLess());
    if (Ids.IsEmpty())
        return Fail(Error, GuideIndex > 0
            ? FText::Format(LOCTEXT("MissingGuideLine", "缺少第 {0} 次夜枭引导台词。"), FText::AsNumber(GuideIndex))
            : LOCTEXT("MissingCasualLines", "尚未配置夜枭的随机闲聊台词。"));
    if (GuideIndex > 0 && Ids.Num() != 1)
        return Fail(Error, LOCTEXT("DuplicateGuideLine", "同一序号存在多条夜枭引导台词。"));
    for (FName Id : Ids)
        if (Catalog.OwlLines.FindChecked(Id).Text.IsEmpty())
            return Fail(Error, LOCTEXT("EmptyOwlLine", "夜枭台词内容尚未填写。"));

    FRandomStream Random = State.CosmeticRandom;
    const FName Chosen = GuideIndex > 0 ? Ids[0] : Ids[Random.RandRange(0, Ids.Num() - 1)];
    OutId = Chosen;
    OutText = Catalog.OwlLines.FindChecked(Chosen).Text;
    State.CosmeticRandom = Random;
    if (State.OwlTalkCount < MAX_int32) ++State.OwlTalkCount;
    return true;
}

#undef LOCTEXT_NAMESPACE
