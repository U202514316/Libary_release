#include "ShopAudioLibrary.h"
#include "ShopAudioComponent.h"
#include "ShopPlayerController.h"
#include "ShopRunSubsystem.h"
#include "Kismet/GameplayStatics.h"

UShopAudioComponent* UShopAudioLibrary::GetShopAudio(const UObject* Context)
{
    AShopPlayerController* PC = Cast<AShopPlayerController>(UGameplayStatics::GetPlayerController(Context, 0));
    return PC ? PC->ShopAudio.Get() : nullptr;
}

void UShopAudioLibrary::PlayWidgetAction(const UObject* Context, FName WidgetBlueprint, FName ButtonName)
{
    UShopAudioComponent* Audio = GetShopAudio(Context);
    if (!Audio) return;
    const FString Button = ButtonName.ToString();
    FName Event = TEXT("UI_Click");
    if (WidgetBlueprint == TEXT("WBP_OwlTutorial")) Event = TEXT("Owl_Advance");
    else if (Button.Contains(TEXT("Close")) || ButtonName == TEXT("BtnBack") || ButtonName == TEXT("BtnSkip")) Event = TEXT("UI_Close");
    else if (ButtonName == TEXT("BtnIntroduction") || ButtonName == TEXT("BtnSell")) Event = TEXT("UI_Page");
    Audio->PlayEvent(Event);
}

void UShopAudioLibrary::PlayWidgetHover(const UObject* Context)
{
    if (UShopAudioComponent* Audio = GetShopAudio(Context)) Audio->PlayEvent(TEXT("UI_Hover"));
}

void UShopAudioLibrary::NotifyAudioScreen(const UObject* Context, FName Screen)
{
    if (UShopAudioComponent* Audio = GetShopAudio(Context)) Audio->NotifyScreen(Screen);
}

void UShopAudioLibrary::AfterUICommand(const UObject* Context, FName Command)
{
    UShopAudioComponent* Audio = GetShopAudio(Context);
    AShopPlayerController* PC = Cast<AShopPlayerController>(UGameplayStatics::GetPlayerController(Context, 0));
    UShopRunSubsystem* Run = PC ? PC->GetShopRun() : nullptr;
    if (!Audio || !Run) return;
    const FShopCommandResult Result = Run->GetLastResult();
    if (!Result.bSucceeded)
    {
        // Wrong sales already emit ResolveCustomer, including the normal failed-sale sound.
        if (!(Result.Code == EShopActionResult::WrongBook && Run->GetRunRules().bWrongBookConsumesCustomer)) Audio->PlayEvent(TEXT("Sale_Wrong"));
        return;
    }
    if (Command == TEXT("RequestRestock")) Audio->PlayEvent(TEXT("Purchase_Normal"));
    else if (Command == TEXT("RequestBuyMarketItem")) Audio->PlayEvent(TEXT("Purchase_Secret"));
    else if (Command == TEXT("RequestReadSecret")) Audio->PlayEvent(TEXT("Read_Secret"));
    else if (Command == TEXT("RequestListSecretBook") || Command == TEXT("RequestUnlistSecretBook")) Audio->PlayEvent(TEXT("UI_Page"));
}
