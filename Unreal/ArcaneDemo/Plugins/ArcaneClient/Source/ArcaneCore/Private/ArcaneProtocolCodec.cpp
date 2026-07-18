#include "ArcaneProtocolCodec.h"
#include "ArcaneWireCodec.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ArcaneProtocolCodec
{
	namespace Private
	{
		static FString BytesToUuidString(const uint8 UuidBytes[16])
		{
			return FString::Printf(
				TEXT("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"),
				UuidBytes[0], UuidBytes[1], UuidBytes[2], UuidBytes[3],
				UuidBytes[4], UuidBytes[5],
				UuidBytes[6], UuidBytes[7],
				UuidBytes[8], UuidBytes[9],
				UuidBytes[10], UuidBytes[11], UuidBytes[12], UuidBytes[13], UuidBytes[14], UuidBytes[15]
			);
		}

		static void StringToUuidBytes(const FString& UuidString, uint8 OutUuidBytes[16])
		{
			FMemory::Memzero(OutUuidBytes, 16);
			// Parse hex string in format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
			const TCHAR* Str = *UuidString;
			for (int i = 0; i < 16 && *Str; ++i)
			{
				// Skip hyphens
				while (*Str && (*Str == TEXT('-'))) ++Str;

				uint8 HighNibble = 0, LowNibble = 0;
				if (*Str >= TEXT('0') && *Str <= TEXT('9'))
					HighNibble = (*Str - TEXT('0')) << 4;
				else if (*Str >= TEXT('a') && *Str <= TEXT('f'))
					HighNibble = ((*Str - TEXT('a') + 10)) << 4;
				else if (*Str >= TEXT('A') && *Str <= TEXT('F'))
					HighNibble = ((*Str - TEXT('A') + 10)) << 4;
				++Str;

				if (*Str >= TEXT('0') && *Str <= TEXT('9'))
					LowNibble = (*Str - TEXT('0'));
				else if (*Str >= TEXT('a') && *Str <= TEXT('f'))
					LowNibble = (*Str - TEXT('a') + 10);
				else if (*Str >= TEXT('A') && *Str <= TEXT('F'))
					LowNibble = (*Str - TEXT('A') + 10);
				++Str;

				OutUuidBytes[i] = HighNibble | LowNibble;
			}
		}

		static FString WireUuidToString(const ArcaneWireCodec::FArcaneWireUUID& Uuid)
		{
			uint8 Bytes[16];
			Uuid.ToBytes(Bytes);
			return BytesToUuidString(Bytes);
		}

		static ArcaneWireCodec::FArcaneWireUUID WireUuidFromString(const FString& UuidString)
		{
			uint8 Bytes[16];
			StringToUuidBytes(UuidString, Bytes);
			return ArcaneWireCodec::FArcaneWireUUID::FromBytes(Bytes);
		}
	}

	bool ParseJoinResponse(const FString& JsonString, FString& OutServerHost, int32& OutServerPort, FString& OutClusterId, FString& OutError)
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

		// Extract ClusterId from join response (may be empty)
		JsonObject->TryGetStringField(TEXT("cluster_id"), OutClusterId);

		return true;
	}

	bool ParseStateUpdate(const TArray<uint8>& Bytes, TArray<FArcaneEntityState>& OutUpdated, TArray<FString>& OutRemovedIds)
	{
		ArcaneWireCodec::FArcaneWireDelta Delta;
		if (!ArcaneWireCodec::DecodeServerFrame(Bytes, Delta))
		{
			return false;
		}

		OutUpdated.Reset();
		for (const ArcaneWireCodec::FArcaneWireEntityState& WireEntity : Delta.Updated)
		{
			FString EntityIdStr = Private::WireUuidToString(WireEntity.EntityId);
			FString ClusterIdStr = Private::WireUuidToString(WireEntity.ClusterId);

			// Convert quantized int16 positions to float with Y/Z coordinate swap (wire Y-up → UE Z-up)
			FVector Position(
				static_cast<float>(WireEntity.Position.X),
				static_cast<float>(WireEntity.Position.Z),
				static_cast<float>(WireEntity.Position.Y)
			);

			FVector Velocity(
				static_cast<float>(WireEntity.Velocity.X),
				static_cast<float>(WireEntity.Velocity.Z),
				static_cast<float>(WireEntity.Velocity.Y)
			);

			OutUpdated.Add(FArcaneEntityState(EntityIdStr, ClusterIdStr, Position, Velocity, WireEntity.UserData));
		}

		OutRemovedIds.Reset();
		for (const ArcaneWireCodec::FArcaneWireUUID& RemovedId : Delta.Removed)
		{
			OutRemovedIds.Add(Private::WireUuidToString(RemovedId));
		}

		return true;
	}

	TArray<uint8> BuildPlayerState(const FString& PlayerEntityId, FVector Position, FVector Velocity, float PositionScale)
	{
		const float Scale = PositionScale > 0.f ? PositionScale : 1.f;

		ArcaneWireCodec::FArcaneWirePlayerState PlayerState;
		PlayerState.EntityId = Private::WireUuidFromString(PlayerEntityId);

		// Quantize position and velocity with Y/Z coordinate swap (UE Z-up → wire Y-up)
		PlayerState.Position = ArcaneWireCodec::FArcaneWireVec3Q(
			static_cast<int16>(Position.X / Scale),
			static_cast<int16>(Position.Z / Scale),
			static_cast<int16>(Position.Y / Scale)
		);

		PlayerState.Velocity = ArcaneWireCodec::FArcaneWireVec3Q(
			static_cast<int16>(Velocity.X / Scale),
			static_cast<int16>(Velocity.Z / Scale),
			static_cast<int16>(Velocity.Y / Scale)
		);

		// user_data left empty for now
		PlayerState.UserData.Empty();

		// Monotonic client_seq for round-trip latency measurement (echoed by server)
		static uint64 NextClientSeq = 0;
		PlayerState.ClientSeq = ++NextClientSeq;

		TArray<uint8> EncodedBytes;
		ArcaneWireCodec::EncodeClientFrame(PlayerState, EncodedBytes);
		return EncodedBytes;
	}
}
