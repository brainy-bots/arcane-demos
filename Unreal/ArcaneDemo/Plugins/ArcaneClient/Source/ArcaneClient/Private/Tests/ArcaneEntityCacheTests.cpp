#if WITH_DEV_AUTOMATION_TESTS

#include "ArcaneEntityCache.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneEntityCacheApplyAndSnapshotTest,
	"ArcaneClient.Cache.ApplyAndSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneEntityCacheApplyAndSnapshotTest::RunTest(const FString& Parameters)
{
	FArcaneEntityCache Cache;
	TArray<FArcaneEntityState> Updated;
	Updated.Add(FArcaneEntityState(TEXT("e1"), TEXT("c1"), FVector(1, 2, 3), FVector(0, 0, 0)));
	Cache.ApplyStateUpdate(Updated, {});

	const TArray<FArcaneEntityState> Snapshot = Cache.GetSnapshot();
	TestEqual(TEXT("One entity in snapshot"), Snapshot.Num(), 1);
	if (Snapshot.Num() == 1)
	{
		TestEqual(TEXT("Entity id matches"), Snapshot[0].EntityId, FString(TEXT("e1")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneEntityCacheRemoveTest,
	"ArcaneClient.Cache.RemoveEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneEntityCacheRemoveTest::RunTest(const FString& Parameters)
{
	FArcaneEntityCache Cache;
	TArray<FArcaneEntityState> Updated;
	Updated.Add(FArcaneEntityState(TEXT("e1"), TEXT("c1"), FVector(1, 2, 3), FVector(0, 0, 0)));
	Cache.ApplyStateUpdate(Updated, {});
	Cache.ApplyStateUpdate({}, { TEXT("e1") });

	const TArray<FArcaneEntityState> Snapshot = Cache.GetSnapshot();
	TestEqual(TEXT("Entity removed"), Snapshot.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArcaneEntityCacheInterpolatedSnapshotTest,
	"ArcaneClient.Cache.InterpolatedSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FArcaneEntityCacheInterpolatedSnapshotTest::RunTest(const FString& Parameters)
{
	FArcaneEntityCache Cache;
	Cache.ApplyStateUpdate({ FArcaneEntityState(TEXT("e1"), TEXT("c1"), FVector(0, 0, 0), FVector(10, 0, 0)) }, {});
	FPlatformProcess::Sleep(0.01f);
	Cache.ApplyStateUpdate({ FArcaneEntityState(TEXT("e1"), TEXT("c1"), FVector(10, 0, 0), FVector(10, 0, 0)) }, {});

	const TArray<FArcaneEntityState> Snapshot = Cache.GetInterpolatedSnapshot(0.005f);
	TestEqual(TEXT("Interpolated snapshot has one entity"), Snapshot.Num(), 1);
	if (Snapshot.Num() == 1)
	{
		TestTrue(TEXT("Interpolated X is finite"), FMath::IsFinite(Snapshot[0].Position.X));
	}
	return true;
}

#endif
