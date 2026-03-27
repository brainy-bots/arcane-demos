#include "ArcaneProtocolCodec.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ArcaneProtocolCodec
{
	bool ParseJoinResponse(const FString& JsonString, FString& OutServerHost, int32& OutServerPort, FString& OutError)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			OutError = TEXT("Invalid JSON from /join");
			return false;
		}

		if (!JsonObject->TryGetStringField(TEXT("server_host"), OutServerHost))
		{
			OutError = TEXT("Join response missing server_host");
			return false;
		}
		OutServerPort = 8080;
		JsonObject->TryGetNumberField(TEXT("server_port"), OutServerPort);
		return true;
	}

	bool ParseStateUpdate(const FString& JsonString, TArray<FArcaneEntityState>& OutUpdated, TArray<FString>& OutRemovedIds)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			return false;
		}

		OutUpdated.Reset();
		const TArray<TSharedPtr<FJsonValue>>* UpdatedArray = nullptr;
		if (JsonObject->TryGetArrayField(TEXT("updated"), UpdatedArray))
		{
			for (const TSharedPtr<FJsonValue>& EntryVal : *UpdatedArray)
			{
				const TSharedPtr<FJsonObject>* EntryObj = nullptr;
				if (!EntryVal->TryGetObject(EntryObj) || !EntryObj->IsValid()) continue;

				FString EntityId;
				if (!(*EntryObj)->TryGetStringField(TEXT("entity_id"), EntityId)) continue;

				FString ClusterId;
				(*EntryObj)->TryGetStringField(TEXT("cluster_id"), ClusterId);

				FVector Position = FVector::ZeroVector;
				const TSharedPtr<FJsonObject>* PosObj = nullptr;
				if ((*EntryObj)->TryGetObjectField(TEXT("position"), PosObj) && PosObj->IsValid())
				{
					(*PosObj)->TryGetNumberField(TEXT("x"), Position.X);
					(*PosObj)->TryGetNumberField(TEXT("y"), Position.Y);
					(*PosObj)->TryGetNumberField(TEXT("z"), Position.Z);
				}

				FVector Velocity = FVector::ZeroVector;
				const TSharedPtr<FJsonObject>* VelObj = nullptr;
				if ((*EntryObj)->TryGetObjectField(TEXT("velocity"), VelObj) && VelObj->IsValid())
				{
					(*VelObj)->TryGetNumberField(TEXT("x"), Velocity.X);
					(*VelObj)->TryGetNumberField(TEXT("y"), Velocity.Y);
					(*VelObj)->TryGetNumberField(TEXT("z"), Velocity.Z);
				}

				OutUpdated.Add(FArcaneEntityState(EntityId, ClusterId, Position, Velocity));
			}
		}

		OutRemovedIds.Reset();
		const TArray<TSharedPtr<FJsonValue>>* RemovedArray = nullptr;
		if (JsonObject->TryGetArrayField(TEXT("removed"), RemovedArray))
		{
			for (const TSharedPtr<FJsonValue>& IdVal : *RemovedArray)
			{
				FString Id;
				if (IdVal->TryGetString(Id))
				{
					OutRemovedIds.Add(Id);
				}
			}
		}
		return true;
	}

	FString BuildPlayerStateJson(const FString& PlayerEntityId, FVector Position, FVector Velocity, float PositionScale)
	{
		const float Scale = PositionScale > 0.f ? PositionScale : 1.f;
		return FString::Printf(
			TEXT("{\"type\":\"PLAYER_STATE\",\"entity_id\":\"%s\",\"position\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"velocity\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f}}"),
			*PlayerEntityId,
			Position.X / Scale, Position.Z / Scale, Position.Y / Scale,
			Velocity.X / Scale, Velocity.Z / Scale, Velocity.Y / Scale
		);
	}
}
