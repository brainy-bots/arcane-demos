#if WITH_DEV_AUTOMATION_TESTS

#include "ArcaneAdapterSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneAdapterReconnectSchedulesRetryTest,
	"ArcaneClient.Lifecycle.ReconnectSchedulesRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneAdapterReconnectSchedulesRetryTest::RunTest(const FString& Parameters)
{
	UArcaneAdapterSubsystem* Subsystem = NewObject<UArcaneAdapterSubsystem>();
	Subsystem->bEnableAutoReconnect = true;
	Subsystem->MaxReconnectAttempts = 3;
	Subsystem->ReconnectDelaySeconds = 1.0f;
	Subsystem->TestOnly_SetManualDisconnect(false);
	Subsystem->TestOnly_SetReconnectAttempt(0);

	Subsystem->TestOnly_InvokeHandleConnectionFailure(TEXT("ws error"));

	TestEqual(TEXT("State becomes reconnecting"), Subsystem->GetConnectionState(), EArcaneConnectionState::Reconnecting);
	TestEqual(TEXT("Attempt increments"), Subsystem->TestOnly_GetReconnectAttempt(), 1);
	TestTrue(TEXT("Reconnect deadline scheduled"), Subsystem->TestOnly_GetNextReconnectAtSeconds() > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneAdapterReconnectExhaustionFailsTest,
	"ArcaneClient.Lifecycle.ReconnectExhaustionFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneAdapterReconnectExhaustionFailsTest::RunTest(const FString& Parameters)
{
	UArcaneAdapterSubsystem* Subsystem = NewObject<UArcaneAdapterSubsystem>();
	Subsystem->bEnableAutoReconnect = true;
	Subsystem->MaxReconnectAttempts = 1;
	Subsystem->TestOnly_SetManualDisconnect(false);
	Subsystem->TestOnly_SetReconnectAttempt(1);

	Subsystem->TestOnly_InvokeHandleConnectionFailure(TEXT("join failed"));

	TestEqual(TEXT("State becomes failed when retries exhausted"), Subsystem->GetConnectionState(), EArcaneConnectionState::Failed);
	TestEqual(TEXT("Attempt does not keep increasing after exhaustion"), Subsystem->TestOnly_GetReconnectAttempt(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneAdapterDisconnectResetsConnectionStateTest,
	"ArcaneClient.Lifecycle.DisconnectResetsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneAdapterDisconnectResetsConnectionStateTest::RunTest(const FString& Parameters)
{
	UArcaneAdapterSubsystem* Subsystem = NewObject<UArcaneAdapterSubsystem>();
	Subsystem->TestOnly_SetConnectionState(EArcaneConnectionState::Reconnecting);
	Subsystem->TestOnly_SetReconnectAttempt(2);

	Subsystem->Disconnect();

	TestEqual(TEXT("Disconnect sets disconnected state"), Subsystem->GetConnectionState(), EArcaneConnectionState::Disconnected);
	TestFalse(TEXT("Disconnect clears connected flag"), Subsystem->IsConnected());
	TestEqual(TEXT("Disconnect clears reconnect deadline"), Subsystem->TestOnly_GetNextReconnectAtSeconds(), 0.0);
	return true;
}

#endif
