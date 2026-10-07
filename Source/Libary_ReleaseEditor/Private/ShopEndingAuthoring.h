#pragma once
#include "CoreMinimal.h"
class UDataTable;
class UWidgetBlueprint;
namespace ShopEndingAuthoring
{
    bool Populate(UDataTable* Table);
    bool UpgradeWidget(UWidgetBlueprint* Blueprint);
    bool UpgradeProject();
}
