// Copyright Arcane Engine. Movement component for the demo character so we can set velocity for replicated entities.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ArcaneDemoCharacterMovementComponent.generated.h"

/**
 * Subclass of UCharacterMovementComponent so we can set Velocity each frame for replicated-entity
 * instances. When the character is a replicated display we skip CalcVelocity so our set velocity
 * is not overwritten; the ABP then sees the correct value (same as standard UE simulated proxies).
 */
UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class ARCANEDEMO_API UArcaneDemoCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Set velocity for this frame (used when character is a replicated display). */
	void SetDisplayVelocity(FVector V);

	/** Set acceleration so ABP conditions (e.g. "has acceleration" for running) pass when externally driven. Call when setting velocity. */
	void SetAccelerationForAnimation(FVector WorldVelocity);

	// Skip normal velocity calculation when character is a replicated display so our SetDisplayVelocity is not overwritten.
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
};
