// Copyright Arcane Engine. Late-update component implementation.

#include "ArcaneEntityDisplayLateUpdateComponent.h"
#include "ArcaneEntityDisplay.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoCharacterMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UArcaneEntityDisplayLateUpdateComponent::UArcaneEntityDisplayLateUpdateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UArcaneEntityDisplayLateUpdateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (AArcaneEntityDisplay* Display = Cast<AArcaneEntityDisplay>(GetOwner()))
	{
		Display->ReapplyVelocityAndAcceleration();
	}
}
