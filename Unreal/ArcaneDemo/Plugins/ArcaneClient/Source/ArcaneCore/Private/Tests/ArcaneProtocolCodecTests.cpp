#if WITH_DEV_AUTOMATION_TESTS

#include "ArcaneProtocolCodec.h"
#include "ArcaneWireCodec.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecJoinParseTest,
	"ArcaneCore.Protocol.ParseJoinResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecJoinParseTest::RunTest(const FString& Parameters)
{
	FString Host;
	int32 Port = 0;
	FString ClusterId;
	FString Error;
	const bool bOk = ArcaneProtocolCodec::ParseJoinResponse(
		TEXT("{\"server_host\":\"127.0.0.1\",\"server_port\":8090,\"cluster_id\":\"550e8400-e29b-41d4-a716-446655440000\"}"),
		Host,
		Port,
		ClusterId,
		Error
	);
	TestTrue(TEXT("Join response parses"), bOk);
	TestEqual(TEXT("Host parsed"), Host, FString(TEXT("127.0.0.1")));
	TestEqual(TEXT("Port parsed"), Port, 8090);
	TestEqual(TEXT("ClusterId parsed"), ClusterId, FString(TEXT("550e8400-e29b-41d4-a716-446655440000")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecJoinParseFailureTest,
	"ArcaneCore.Protocol.ParseJoinResponseFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecJoinParseFailureTest::RunTest(const FString& Parameters)
{
	FString Host;
	int32 Port = 0;
	FString ClusterId;
	FString Error;
	const bool bOk = ArcaneProtocolCodec::ParseJoinResponse(
		TEXT("{\"server_port\":8090}"),
		Host,
		Port,
		ClusterId,
		Error
	);
	TestFalse(TEXT("Join response without host fails"), bOk);
	TestTrue(TEXT("Error text present"), !Error.IsEmpty());

	const bool bWrongType = ArcaneProtocolCodec::ParseJoinResponse(
		TEXT("{\"server_host\":123,\"server_port\":8090}"),
		Host,
		Port,
		ClusterId,
		Error
	);
	TestFalse(TEXT("Join response with non-string host fails"), bWrongType);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecStateUpdateParseTest,
	"ArcaneCore.Protocol.ParseStateUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecStateUpdateParseTest::RunTest(const FString& Parameters)
{
	// Build a server delta via the wire codec (same FlatBuffer layout the Rust
	// cluster produces), then parse it through ParseStateUpdate.
	ArcaneWireCodec::FArcaneWireDelta Delta;
	Delta.SourceClusterId = ArcaneWireCodec::FArcaneWireUUID(1, 0);
	Delta.Seq = 1;
	Delta.Tick = 1;
	Delta.Timestamp = 0.0;

	ArcaneWireCodec::FArcaneWireEntityState Entity;
	// entity_id = 16 bytes of 0x01, cluster_id = 16 bytes of 0x02
	uint8 EntityIdBytes[16];
	uint8 ClusterIdBytes[16];
	FMemory::Memset(EntityIdBytes, 0x01, 16);
	FMemory::Memset(ClusterIdBytes, 0x02, 16);
	Entity.EntityId = ArcaneWireCodec::FArcaneWireUUID::FromBytes(EntityIdBytes);
	Entity.ClusterId = ArcaneWireCodec::FArcaneWireUUID::FromBytes(ClusterIdBytes);
	Entity.Position = ArcaneWireCodec::FArcaneWireVec3Q(1, 2, 3);
	Entity.Velocity = ArcaneWireCodec::FArcaneWireVec3Q(4, 5, 6);
	Entity.UserData = { 0xAA, 0xBB, 0xCC, 0xDD };
	Delta.Updated.Add(Entity);

	uint8 RemovedIdBytes[16];
	FMemory::Memset(RemovedIdBytes, 0xFF, 16);
	Delta.Removed.Add(ArcaneWireCodec::FArcaneWireUUID::FromBytes(RemovedIdBytes));

	TArray<uint8> Bytes;
	TestTrue(TEXT("Server delta encodes"), ArcaneWireCodec::EncodeServerDelta(Delta, Bytes));

	TArray<FArcaneEntityState> Updated;
	TArray<FString> Removed;
	const bool bOk = ArcaneProtocolCodec::ParseStateUpdate(Bytes, Updated, Removed);
	TestTrue(TEXT("State update parses"), bOk);
	TestEqual(TEXT("One updated entity"), Updated.Num(), 1);
	TestEqual(TEXT("One removed id"), Removed.Num(), 1);
	if (Updated.Num() == 1)
	{
		TestEqual(TEXT("EntityId string"), Updated[0].EntityId, FString(TEXT("01010101-0101-0101-0101-010101010101")));
		TestEqual(TEXT("ClusterId string"), Updated[0].ClusterId, FString(TEXT("02020202-0202-0202-0202-020202020202")));

		// Wire position (1, 2, 3) → UE position (X=1, Y=3, Z=2) due to coordinate swap
		TestTrue(TEXT("Position x"), FMath::IsNearlyEqual(Updated[0].Position.X, 1.0));
		TestTrue(TEXT("Position y (wire z)"), FMath::IsNearlyEqual(Updated[0].Position.Y, 3.0));
		TestTrue(TEXT("Position z (wire y)"), FMath::IsNearlyEqual(Updated[0].Position.Z, 2.0));

		// Verify user_data is passed through
		TestEqual(TEXT("UserData length"), Updated[0].UserData.Num(), 4);
		if (Updated[0].UserData.Num() == 4)
		{
			TestEqual(TEXT("UserData[0]"), Updated[0].UserData[0], (uint8)0xAA);
			TestEqual(TEXT("UserData[1]"), Updated[0].UserData[1], (uint8)0xBB);
			TestEqual(TEXT("UserData[2]"), Updated[0].UserData[2], (uint8)0xCC);
			TestEqual(TEXT("UserData[3]"), Updated[0].UserData[3], (uint8)0xDD);
		}
	}
	if (Removed.Num() == 1)
	{
		TestEqual(TEXT("Removed id string"), Removed[0], FString(TEXT("ffffffff-ffff-ffff-ffff-ffffffffffff")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecStateUpdateFailureTest,
	"ArcaneCore.Protocol.ParseStateUpdateFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecStateUpdateFailureTest::RunTest(const FString& Parameters)
{
	TArray<FArcaneEntityState> Updated;
	TArray<FString> Removed;

	// Malformed bytes (not a valid FlatBuffer)
	TArray<uint8> BadBytes;
	BadBytes.Add(99);
	const bool bOk = ArcaneProtocolCodec::ParseStateUpdate(BadBytes, Updated, Removed);
	TestFalse(TEXT("Invalid bytes fail"), bOk);
	TestEqual(TEXT("No entities parsed"), Updated.Num(), 0);
	TestEqual(TEXT("No removed parsed"), Removed.Num(), 0);

	// Empty bytes
	TArray<uint8> EmptyBytes;
	const bool bEmpty = ArcaneProtocolCodec::ParseStateUpdate(EmptyBytes, Updated, Removed);
	TestFalse(TEXT("Empty bytes fail"), bEmpty);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecBuildPlayerStateTest,
	"ArcaneCore.Protocol.BuildPlayerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecBuildPlayerStateTest::RunTest(const FString& Parameters)
{
	// Build player state and decode it back through the wire codec.
	const TArray<uint8> Bytes = ArcaneProtocolCodec::BuildPlayerState(
		TEXT("550e8400-e29b-41d4-a716-446655440000"),
		FVector(10.f, 20.f, 30.f),
		FVector(2.f, 4.f, 6.f),
		2.f
	);
	TestTrue(TEXT("Encoded frame non-empty"), Bytes.Num() > 0);

	ArcaneWireCodec::FArcaneWirePlayerState Decoded;
	const bool bOk = ArcaneWireCodec::DecodeClientFrame(Bytes, Decoded);
	TestTrue(TEXT("Client frame decodes"), bOk);

	// Entity id round-trips (hyphens stripped, little-endian lo/hi)
	uint8 IdBytes[16];
	Decoded.EntityId.ToBytes(IdBytes);
	const uint8 Expected[16] = { 0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4, 0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00 };
	bool bIdMatches = true;
	for (int i = 0; i < 16; ++i)
	{
		bIdMatches &= (IdBytes[i] == Expected[i]);
	}
	TestTrue(TEXT("EntityId bytes round-trip"), bIdMatches);

	// Position with coordinate swap and scale: UE (10, 20, 30) / 2 → wire (5, 15, 10)
	TestEqual(TEXT("Position X (UE X) scaled"), Decoded.Position.X, (int16)5);
	TestEqual(TEXT("Position Y (UE Z) scaled"), Decoded.Position.Y, (int16)15);
	TestEqual(TEXT("Position Z (UE Y) scaled"), Decoded.Position.Z, (int16)10);

	// Velocity: UE (2, 4, 6) / 2 → wire (1, 3, 2)
	TestEqual(TEXT("Velocity X"), Decoded.Velocity.X, (int16)1);
	TestEqual(TEXT("Velocity Y (UE Z)"), Decoded.Velocity.Y, (int16)3);
	TestEqual(TEXT("Velocity Z (UE Y)"), Decoded.Velocity.Z, (int16)2);

	// client_seq increments monotonically across calls
	const TArray<uint8> Bytes2 = ArcaneProtocolCodec::BuildPlayerState(
		TEXT("550e8400-e29b-41d4-a716-446655440000"),
		FVector::ZeroVector,
		FVector::ZeroVector,
		1.f
	);
	ArcaneWireCodec::FArcaneWirePlayerState Decoded2;
	TestTrue(TEXT("Second frame decodes"), ArcaneWireCodec::DecodeClientFrame(Bytes2, Decoded2));
	TestTrue(TEXT("client_seq increments"), Decoded2.ClientSeq > Decoded.ClientSeq);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneWireCodecUuidRoundTripTest,
	"ArcaneCore.Wire.UuidRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneWireCodecUuidRoundTripTest::RunTest(const FString& Parameters)
{
	uint8 Bytes[16];
	for (int i = 0; i < 16; ++i)
	{
		Bytes[i] = static_cast<uint8>(i * 17);  // 0x00, 0x11, 0x22, ...
	}
	const ArcaneWireCodec::FArcaneWireUUID Uuid = ArcaneWireCodec::FArcaneWireUUID::FromBytes(Bytes);

	uint8 OutBytes[16];
	Uuid.ToBytes(OutBytes);
	bool bMatches = true;
	for (int i = 0; i < 16; ++i)
	{
		bMatches &= (OutBytes[i] == Bytes[i]);
	}
	TestTrue(TEXT("UUID bytes round-trip"), bMatches);

	// String round-trip (32 hex chars, byte order preserved)
	const FString Str = Uuid.ToString();
	TestEqual(TEXT("Hex string length"), Str.Len(), 32);
	const ArcaneWireCodec::FArcaneWireUUID Parsed = ArcaneWireCodec::FArcaneWireUUID::FromString(Str);
	TestTrue(TEXT("String round-trip"), Parsed == Uuid);

	return true;
}

#endif
