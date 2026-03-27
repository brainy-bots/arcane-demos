#if WITH_DEV_AUTOMATION_TESTS

#include "ArcaneProtocolCodec.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecJoinParseTest,
	"ArcaneClient.Protocol.ParseJoinResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecJoinParseTest::RunTest(const FString& Parameters)
{
	FString Host;
	int32 Port = 0;
	FString Error;
	const bool bOk = ArcaneProtocolCodec::ParseJoinResponse(
		TEXT("{\"server_host\":\"127.0.0.1\",\"server_port\":8090}"),
		Host,
		Port,
		Error
	);
	TestTrue(TEXT("Join response parses"), bOk);
	TestEqual(TEXT("Host parsed"), Host, FString(TEXT("127.0.0.1")));
	TestEqual(TEXT("Port parsed"), Port, 8090);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecStateUpdateParseTest,
	"ArcaneClient.Protocol.ParseStateUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecStateUpdateParseTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT("{\"updated\":[{\"entity_id\":\"e1\",\"cluster_id\":\"c1\",\"position\":{\"x\":1,\"y\":2,\"z\":3},\"velocity\":{\"x\":4,\"y\":5,\"z\":6}}],\"removed\":[\"dead1\"]}");
	TArray<FArcaneEntityState> Updated;
	TArray<FString> Removed;
	const bool bOk = ArcaneProtocolCodec::ParseStateUpdate(Json, Updated, Removed);
	TestTrue(TEXT("State update parses"), bOk);
	TestEqual(TEXT("One updated entity"), Updated.Num(), 1);
	TestEqual(TEXT("One removed id"), Removed.Num(), 1);
	if (Updated.Num() == 1)
	{
		TestEqual(TEXT("Entity id"), Updated[0].EntityId, FString(TEXT("e1")));
		TestEqual(TEXT("Cluster id"), Updated[0].ClusterId, FString(TEXT("c1")));
		TestTrue(TEXT("Position x"), FMath::IsNearlyEqual(Updated[0].Position.X, 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneProtocolCodecBuildPlayerStateTest,
	"ArcaneClient.Protocol.BuildPlayerStateJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneProtocolCodecBuildPlayerStateTest::RunTest(const FString& Parameters)
{
	const FString Json = ArcaneProtocolCodec::BuildPlayerStateJson(
		TEXT("p1"),
		FVector(10.f, 20.f, 30.f),
		FVector(1.f, 2.f, 3.f),
		2.f
	);
	TestTrue(TEXT("Contains message type"), Json.Contains(TEXT("\"type\":\"PLAYER_STATE\"")));
	TestTrue(TEXT("Contains player id"), Json.Contains(TEXT("\"entity_id\":\"p1\"")));
	// Y/Z are intentionally swapped for server coordinate conventions.
	TestTrue(TEXT("Contains scaled x"), Json.Contains(TEXT("\"x\":5.0000")));
	return true;
}

#endif
