#pragma once

#include "CoreMinimal.h"
#include "ArcaneTypes.h"

class FArcaneEntityCache
{
public:
	void Reset();
	void ApplyStateUpdate(const TArray<FArcaneEntityState>& Updated, const TArray<FString>& RemovedIds);
	TArray<FArcaneEntityState> GetSnapshot() const;
	TArray<FArcaneEntityState> GetInterpolatedSnapshot(float InterpolationDelaySeconds) const;

private:
	mutable FCriticalSection Mutex;
	TMap<FString, FArcaneEntityState> EntityCache;
	TMap<FString, FArcaneEntityState> PreviousEntityCache;
	double CurrentSnapshotTime = 0.0;
	double PreviousSnapshotTime = 0.0;
};
