#pragma once

#include "CoreMinimal.h"
#include "ArcaneTypes.h"

namespace ArcaneProtocolCodec
{
	bool ParseJoinResponse(const FString& JsonString, FString& OutServerHost, int32& OutServerPort, FString& OutError);
	bool ParseStateUpdate(const FString& JsonString, TArray<FArcaneEntityState>& OutUpdated, TArray<FString>& OutRemovedIds);
	FString BuildPlayerStateJson(const FString& PlayerEntityId, FVector Position, FVector Velocity, float PositionScale);
}
