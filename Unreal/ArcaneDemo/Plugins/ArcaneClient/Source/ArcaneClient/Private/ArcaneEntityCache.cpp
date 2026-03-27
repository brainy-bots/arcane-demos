#include "ArcaneEntityCache.h"

#include "HAL/PlatformTime.h"

void FArcaneEntityCache::Reset()
{
	FScopeLock Lock(&Mutex);
	EntityCache.Empty();
	PreviousEntityCache.Empty();
	CurrentSnapshotTime = 0.0;
	PreviousSnapshotTime = 0.0;
}

void FArcaneEntityCache::ApplyStateUpdate(const TArray<FArcaneEntityState>& Updated, const TArray<FString>& RemovedIds)
{
	FScopeLock Lock(&Mutex);
	PreviousEntityCache = EntityCache;
	PreviousSnapshotTime = CurrentSnapshotTime;
	CurrentSnapshotTime = FPlatformTime::Seconds();

	// Server sends full snapshot every tick; replace cache with it so client stays in sync.
	if (Updated.Num() > 0)
	{
		EntityCache.Empty();
		for (const FArcaneEntityState& State : Updated)
		{
			EntityCache.Add(State.EntityId, State);
		}
	}
	for (const FString& Id : RemovedIds)
	{
		EntityCache.Remove(Id);
	}
}

TArray<FArcaneEntityState> FArcaneEntityCache::GetSnapshot() const
{
	FScopeLock Lock(&Mutex);
	TArray<FArcaneEntityState> Out;
	for (const auto& Pair : EntityCache)
	{
		Out.Add(Pair.Value);
	}
	return Out;
}

TArray<FArcaneEntityState> FArcaneEntityCache::GetInterpolatedSnapshot(float InterpolationDelaySeconds) const
{
	FScopeLock Lock(&Mutex);
	TArray<FArcaneEntityState> Out;
	const double Now = FPlatformTime::Seconds();
	const double RenderTime = Now - static_cast<double>(InterpolationDelaySeconds);

	for (const auto& Pair : EntityCache)
	{
		const FArcaneEntityState& Current = Pair.Value;
		const FArcaneEntityState* PrevState = PreviousEntityCache.Find(Pair.Key);

		if (!PrevState || PreviousSnapshotTime >= CurrentSnapshotTime)
		{
			if (RenderTime >= CurrentSnapshotTime && CurrentSnapshotTime > 0.0)
			{
				const double Dt = RenderTime - CurrentSnapshotTime;
				FArcaneEntityState Extrapolated = Current;
				Extrapolated.Position += Current.Velocity * Dt;
				Out.Add(Extrapolated);
			}
			else
			{
				Out.Add(Current);
			}
			continue;
		}

		const double T0 = PreviousSnapshotTime;
		const double T1 = CurrentSnapshotTime;
		if (RenderTime <= T0)
		{
			Out.Add(*PrevState);
			continue;
		}
		if (RenderTime >= T1)
		{
			Out.Add(Current);
			continue;
		}

		const double T = (RenderTime - T0) / (T1 - T0);
		FArcaneEntityState Interp;
		Interp.EntityId = Current.EntityId;
		Interp.ClusterId = Current.ClusterId;
		Interp.Position = FMath::Lerp(PrevState->Position, Current.Position, static_cast<float>(T));
		Interp.Velocity = FMath::Lerp(PrevState->Velocity, Current.Velocity, static_cast<float>(T));
		Out.Add(Interp);
	}
	return Out;
}
