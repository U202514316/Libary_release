#pragma once

namespace ShopUIData
{
    // Existing values are preserved unless an explicit migration is requested.
    // That migration changes only MaxDays and the three customer-arrival settings in UI rules.
    // Unique-portrait migration changes only bUniqueDailyCustomerPortraits in UI rules.
    bool Build(bool bUpgradeCounterFlow = false, bool bUpgradeUniquePortraits = false);
}
