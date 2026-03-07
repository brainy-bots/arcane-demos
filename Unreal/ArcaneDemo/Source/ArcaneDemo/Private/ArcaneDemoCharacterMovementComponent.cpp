// Copyright Arcane Engine. Movement component implementation.

#include "ArcaneDemoCharacterMovementComponent.h"
#include "ArcaneDemoCharacter.h"

void UArcaneDemoCharacterMovementComponent::SetDisplayVelocity(FVector V)
{
	Velocity = V;
}

void UArcaneDemoCharacterMovementComponent::SetAccelerationForAnimation(FVector WorldVelocity)
{
	// ABP often requires "has acceleration" to transition to running; when externally driven we only set Velocity.
	if (WorldVelocity.SizeSquared() > 0.01f)
	{
		Acceleration = WorldVelocity.GetSafeNormal() * 100.f;
	}
	else
	{
		Acceleration = FVector::ZeroVector;
	}
}

void UArcaneDemoCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (AArcaneDemoCharacter* DemoChar = Cast<AArcaneDemoCharacter>(CharacterOwner))
	{
		if (DemoChar->bExternallyDriven)
		{
			return; // Velocity set by Arcane each frame; don't overwrite so ABP sees correct Speed for animations
		}
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}
