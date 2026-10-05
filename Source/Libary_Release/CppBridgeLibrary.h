#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CppBridgeLibrary.generated.h"

/** Small examples for calling native C++ from Blueprints. */
UCLASS()
class LIBARY_RELEASE_API UCppBridgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns a greeting and logs it so the C++ call can be verified. */
	UFUNCTION(BlueprintCallable, Category = "Cpp Bridge", meta = (DisplayName = "Cpp Say Hello", Keywords = "cpp hello test"))
	static FString SayHello(const FString& PlayerName);
};
