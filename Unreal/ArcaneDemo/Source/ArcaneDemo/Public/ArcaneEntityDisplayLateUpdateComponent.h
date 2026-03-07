// Copyright Arcane Engine. Late-update component so entity Velocity/Acceleration are set after CMC ticks, for ABP.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArcaneEntityDisplayLateUpdateComponent.generated.h"

class AArcaneEntityDisplay;

/**
 * Ticks in TG_PostUpdateWork so that after all character/movement ticks, we re-apply Velocity and Acceleration
 * to entity characters. This matches the standard pattern: replicated movement is applied; animation reads
 * from the movement component; the CMC can overwrite our values during its tick, so we re-apply for the ABP.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARCANEDEMO_API UArcaneEntityDisplayLateUpdateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UArcaneEntityDisplayLateUpdateComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
