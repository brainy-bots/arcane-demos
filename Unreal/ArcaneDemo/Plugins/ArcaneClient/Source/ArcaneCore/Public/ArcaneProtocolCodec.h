#pragma once

#include "CoreMinimal.h"
#include "ArcaneTypes.h"

namespace ArcaneProtocolCodec
{
	ARCANECORE_API bool ParseJoinResponse(const FString& JsonString, FString& OutServerHost, int32& OutServerPort, FString& OutClusterId, FString& OutError);

	// Binary postcard protocol
	ARCANECORE_API bool ParseStateUpdate(const TArray<uint8>& Bytes, TArray<FArcaneEntityState>& OutUpdated, TArray<FString>& OutRemovedIds);
	ARCANECORE_API TArray<uint8> BuildPlayerState(const FString& PlayerEntityId, FVector Position, FVector Velocity, float PositionScale);
}
