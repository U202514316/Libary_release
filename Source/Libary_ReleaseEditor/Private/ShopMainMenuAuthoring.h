#pragma once

class UWidgetBlueprint;

namespace ShopMainMenuAuthoring
{
    // Replace only the Designer presentation. Existing button instances/events stay intact.
    bool Apply(UWidgetBlueprint* Blueprint);
    bool UpgradeProject();
}
