// Copyright Arcane Engine. Syncs Velocity/Acceleration to CMC after CMC ticks, before mesh/ABP ticks.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArcaneEntityMovementSyncComponent.generated.h"

class UCharacterMovementComponent;
class UArcaneDemoCharacterMovementComponent;

/**
 * On entity characters: ticks after the Character Movement Component and before the skeletal mesh.
 * Copies PendingVelocity into the CMC so the Animation Blueprint sees correct Velocity/Acceleration
 * when it runs (mesh tick is after this component via prerequisite).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARCANEDEMO_API UArcaneEntityMovementSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UArcaneEntityMovementSyncComponent();

	/** Set by ArcaneEntityDisplay each frame; applied to CMC in TickComponent (after CMC tick, before mesh). */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane")
	FVector PendingVelocity = FVector::ZeroVector;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Call after creating the component on a character to set tick order: Movement -> this -> Mesh. */
	void SetTickOrderAfterMovementAndBeforeMesh();
};
