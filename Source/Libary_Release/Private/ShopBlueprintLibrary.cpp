#include "ShopBlueprintLibrary.h"
#include "ShopRunSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

UShopRunSubsystem* UShopBlueprintLibrary::GetShopService(const UObject* WorldContextObject)
{
    UGameInstance* Instance = UGameplayStatics::GetGameInstance(WorldContextObject);
    return Instance ? Instance->GetSubsystem<UShopRunSubsystem>() : nullptr;
}
FText UShopBlueprintLibrary::GetBookTypeText(EBookType Type)
{
    switch (Type)
    {
    case EBookType::Novel: return FText::FromString(TEXT("小说"));
    case EBookType::Poem: return FText::FromString(TEXT("诗集"));
    case EBookType::History: return FText::FromString(TEXT("史书"));
    case EBookType::Secret: return FText::FromString(TEXT("秘密书"));
    default: return FText::GetEmpty();
    }
}
