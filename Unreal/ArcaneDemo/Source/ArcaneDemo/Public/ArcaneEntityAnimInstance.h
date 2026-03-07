// Copyright Arcane Engine. AnimInstance for entity visuals: exposes Speed for run/idle blending.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Engine/StreamableManager.h"
#include "ArcaneEntityAnimInstance.generated.h"

class UAnimMontage;

/**
 * Animation instance for Arcane entity visuals. Exposes Speed (velocity magnitude).
 * Option A: Create an AnimBlueprint that parents from this class and blends Idle/Run by Speed.
 * Option B: Set RunMontagePath to a run montage (e.g. from template); this class will play it when Speed > 0.
 */
UCLASS(Transient)
class ARCANEDEMO_API UArcaneEntityAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Magnitude of movement velocity; set each tick from owner GetVelocity(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	float Speed = 0.f;

	/** World-space velocity; set from owner for blend trees that use it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	FVector Velocity = FVector::ZeroVector;

	/** True when velocity Z < -1 (falling); for jump/land. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bIsFalling = false;

	/** Optional: path to a run montage (e.g. from Third Person template). When set, we play it when Speed > 0 so character visibly runs without an ABP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	FSoftObjectPath RunMontagePath;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedRunMontage;
	/** So we only try loading the montage once per instance (avoids hundreds of load attempts per frame when asset is missing). */
	bool bRunMontageLoadAttempted = false;
};
