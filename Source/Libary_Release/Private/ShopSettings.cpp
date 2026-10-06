#include "ShopSettings.h"

UShopSettings::UShopSettings()
{
    Books = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Books.DT_Books"));
    Customers = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Customers.DT_Customers"));
    RunRules = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_RunRules.DT_RunRules"));
    Decrees = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Decrees.DT_Decrees"));
    Events = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Events.DT_Events"));
    MarketItems = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_MarketItems.DT_MarketItems"));
    OwlLines = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Owl.DT_Owl"));
    Endings = FSoftObjectPath(TEXT("/Game/ProgramA/Release/Data/DT_Endings.DT_Endings"));
}
