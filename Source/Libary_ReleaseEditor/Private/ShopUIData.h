#pragma once

namespace ShopUIData
{
    // Existing values are preserved unless an explicit migration is requested.
    // Counter-flow migration changes only the three customer-arrival settings in UI rules.
    // Unique-portrait migration changes only bUniqueDailyCustomerPortraits in UI rules.
    bool Build(bool bUpgradeCounterFlow = false, bool bUpgradeUniquePortraits = false);
    bool UpgradeHistoryMarketDecrees();
    // Restore the authored calendar without changing any other existing rules or UI assets.
    bool Restore35Days();
}
