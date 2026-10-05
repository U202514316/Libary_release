#include "ShopSettings.h"

UShopSettings::UShopSettings()
{
    Books = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_Books.DT_Books"));
    Customers = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_Customers.DT_Customers"));
    RunRules = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_RunRules.DT_RunRules"));
    Decrees = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_Decrees.DT_Decrees"));
    Events = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_Events.DT_Events"));
    MarketItems = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_MarketItems.DT_MarketItems"));
    OwlLines = FSoftObjectPath(TEXT("/Game/ProgramA/Prototype/Data/DT_Owl.DT_Owl"));
}
