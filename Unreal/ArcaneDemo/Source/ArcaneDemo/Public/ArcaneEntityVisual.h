// Copyright Arcane Engine. One visual actor per remote entity: skeletal mesh + run/idle animation, tint by cluster.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Engine/StreamableManager.h"
#include "ArcaneEntityVisual.generated.h"

class USkeletalMeshComponent;
class UCapsuleComponent;
class USphereComponent;
class UAnimInstance;
class UMaterialInstanceDynamic;
class UArcaneEntityMovementComponent;

/**
 * One instance per Arcane entity. Uses a movement component so GetVelocity() (and ABPs that read
 * "Get Pawn Owner" -> "Get Velocity" or movement component velocity) see replicated velocity for walk/run/jump.
 */
UCLASS()
class ARCANEDEMO_API AArcaneEntityVisual : public APawn
{
	GENERATED_BODY()

public:
	AArcaneEntityVisual();

	/** Skeletal mesh for this entity (e.g. mannequin). Hidden when using simple fallback. */
	UPROPERTY(VisibleAnywhere, Category = "Arcane")
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	/** When no skeletal mesh is available: capsule (body) + sphere (head) so humanoids still appear on the surface. */
	UPROPERTY(VisibleAnywhere, Category = "Arcane")
	TObjectPtr<UCapsuleComponent> FallbackCapsule;
	UPROPERTY(VisibleAnywhere, Category = "Arcane")
	TObjectPtr<USphereComponent> FallbackHead;

	/** Set world position and rotation from velocity; updates movement component so GetVelocity() returns it for ABPs. */
	void SetEntityTransform(const FVector& WorldPosition, const FVector& WorldVelocity);

	/** Apply cluster color to the mesh (dynamic material tint). */
	void SetClusterColor(FLinearColor Color);

	/** Optional: set mesh from path; if load fails, enables simple humanoid fallback (capsule+sphere). */
	void SetMeshFromPath(const FSoftObjectPath& MeshPath);
	/** Store path to load in BeginPlay (so mesh is set after actor is fully in world — fixes PIE). */
	void SetMeshPathToLoad(const FSoftObjectPath& MeshPath);
	void SetAnimClass(TSubclassOf<UAnimInstance> AnimClass);
	/** Build capsule body + sphere head with tintable material (no project content required). */
	void SetupSimpleHumanoidFallback();
	/** Optional: run montage path for C++ anim instance (play when Speed > 0). */
	void SetRunMontagePath(const FSoftObjectPath& MontagePath);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Force mesh/fallback visible and mark render state dirty (helps PIE). */
	void ForceVisibilityForPIE();

	/** Cached dynamic material instance for tinting (mesh and/or fallback). */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** Run montage path to apply to anim instance when available. */
	FSoftObjectPath StoredRunMontagePath;

	/** Movement component we update each frame so GetVelocity() returns replicated velocity for ABPs. */
	UPROPERTY(VisibleAnywhere, Category = "Arcane")
	TObjectPtr<UArcaneEntityMovementComponent> DisplayMovement;

	/** True when using capsule+sphere instead of skeletal mesh. */
	bool bUsingSimpleFallback = false;

	/** Frames we've ticked for visibility re-apply (then we stop ticking). */
	int32 TickVisibilityFrames = 0;

	/** Mesh path to apply in BeginPlay (deferred so PIE picks up the component). */
	FSoftObjectPath PendingMeshPathToLoad;
};
