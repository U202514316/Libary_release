#include "CppBridgeLibrary.h"

FString UCppBridgeLibrary::SayHello(const FString& PlayerName)
{
	const FString Name = PlayerName.IsEmpty() ? TEXT("Unreal") : PlayerName;
	const FString Message = FString::Printf(TEXT("Hello, %s! C++ is working."), *Name);

	UE_LOG(LogTemp, Log, TEXT("[CppBridge] %s"), *Message);
	return Message;
}
