// Copyright Arcane Engine. Movement component for entity visuals so ABPs see velocity.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "ArcaneEntityMovementComponent.generated.h"

/**
 * Minimal movement component for entity visuals. We set Velocity each frame from replicated state
 * so that APawn::GetVelocity() (and any ABP using "Get Pawn Owner" -> "Get Velocity" or
 * "Get Character Movement" -> velocity) sees the correct value for walk/run/jump blending.
 */
UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class ARCANEDEMO_API UArcaneEntityMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	/** Set velocity for this frame so GetVelocity() and animation systems see it. */
	void SetDisplayVelocity(FVector V);
};
