// Copyright Arcane Engine. Sync component implementation.

#include "ArcaneEntityMovementSyncComponent.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

UArcaneEntityMovementSyncComponent::UArcaneEntityMovementSyncComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UArcaneEntityMovementSyncComponent::SetTickOrderAfterMovementAndBeforeMesh()
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return;

	UCharacterMovementComponent* MC = Char->GetCharacterMovement();
	USkeletalMeshComponent* Mesh = Char->GetMesh();
	if (MC)
	{
		PrimaryComponentTick.AddPrerequisite(MC, MC->PrimaryComponentTick);
	}
	if (Mesh)
	{
		Mesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
}

void UArcaneEntityMovementSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return;

	UCharacterMovementComponent* MC = Char->GetCharacterMovement();
	if (!MC) return;

	MC->Velocity = PendingVelocity;
	if (UArcaneDemoCharacterMovementComponent* DemoMC = Cast<UArcaneDemoCharacterMovementComponent>(MC))
	{
		DemoMC->SetAccelerationForAnimation(PendingVelocity);
	}
}
