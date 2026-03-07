// Copyright Arcane Engine. Entity visual implementation.

#include "ArcaneEntityVisual.h"
#include "ArcaneEntityAnimInstance.h"
#include "ArcaneEntityMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Animation/AnimInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
#include "Logging/LogMacros.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneVisual, Log, All);

AArcaneEntityVisual::AArcaneEntityVisual()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	// Use a scene root so when we hide the mesh (fallback mode), attached fallback components stay visible
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(Root);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetVisibility(true);
	MeshComponent->SetHiddenInGame(false);
	// Movement component so GetVelocity() returns replicated velocity (ABPs that use Get Pawn Owner -> Get Velocity or movement component see it).
	DisplayMovement = CreateDefaultSubobject<UArcaneEntityMovementComponent>(TEXT("DisplayMovement"));
	// Fallback created in SetupSimpleHumanoidFallback when mesh fails to load
	FallbackCapsule = nullptr;
	FallbackHead = nullptr;
}

void AArcaneEntityVisual::BeginPlay()
{
	Super::BeginPlay();
	// If mesh was deferred (e.g. SetMeshPathToLoad used), apply it here; otherwise Display already called SetMeshFromPath at spawn.
	if (PendingMeshPathToLoad.IsValid())
	{
		SetMeshFromPath(PendingMeshPathToLoad);
		PendingMeshPathToLoad.Reset();
	}
	ForceVisibilityForPIE();
}

void AArcaneEntityVisual::SetMeshPathToLoad(const FSoftObjectPath& MeshPath)
{
	PendingMeshPathToLoad = MeshPath;
}

void AArcaneEntityVisual::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ForceVisibilityForPIE();
	TickVisibilityFrames++;
	if (TickVisibilityFrames >= 5)
	{
		PrimaryActorTick.bCanEverTick = false;
	}
}

void AArcaneEntityVisual::ForceVisibilityForPIE()
{
	SetActorHiddenInGame(false);
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
		MeshComponent->SetOwnerNoSee(false);
		MeshComponent->MarkRenderStateDirty();
	}
	if (FallbackCapsule)
	{
		FallbackCapsule->SetVisibility(true);
		FallbackCapsule->SetHiddenInGame(false);
		FallbackCapsule->MarkRenderStateDirty();
	}
	if (FallbackHead)
	{
		FallbackHead->SetVisibility(true);
		FallbackHead->SetHiddenInGame(false);
		FallbackHead->MarkRenderStateDirty();
	}
}

void AArcaneEntityVisual::SetEntityTransform(const FVector& WorldPosition, const FVector& WorldVelocity)
{
	SetActorLocation(WorldPosition);
	if (DisplayMovement)
	{
		DisplayMovement->SetDisplayVelocity(WorldVelocity);
	}
	float Speed = WorldVelocity.Size();
	const float MinSpeedForRotation = 0.05f;
	if (Speed > MinSpeedForRotation)
	{
		FRotator Rot = WorldVelocity.Rotation();
		SetActorRotation(FRotator(0.f, Rot.Yaw, 0.f));
	}
	UAnimInstance* Anim = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
	if (Anim)
	{
		if (UArcaneEntityAnimInstance* ArcaneAnim = Cast<UArcaneEntityAnimInstance>(Anim))
		{
			ArcaneAnim->Speed = Speed;
			if (StoredRunMontagePath.IsValid())
			{
				ArcaneAnim->RunMontagePath = StoredRunMontagePath;
			}
		}
		else
		{
			// Player's ABP (e.g. ABP_Unarmed): set Speed (and optionally Velocity / bIsFalling) so blend tree and jump/fall react.
			static const FName SpeedNames[] = { TEXT("Speed"), TEXT("GroundSpeed"), TEXT("MovementSpeed") };
			for (const FName& Name : SpeedNames)
			{
				if (FProperty* P = Anim->GetClass()->FindPropertyByName(Name))
				{
					if (FFloatProperty* FProp = CastField<FFloatProperty>(P))
					{
						void* ValPtr = P->ContainerPtrToValuePtr<void>(Anim);
						if (ValPtr)
						{
							FProp->SetFloatingPointPropertyValue(ValPtr, Speed);
							break;
						}
					}
				}
			}
			// Some ABPs have a Velocity (vector) or bIsFalling; set them so locomotion and jump/land match.
			if (FProperty* VP = Anim->GetClass()->FindPropertyByName(TEXT("Velocity")))
			{
				FStructProperty* SP = CastField<FStructProperty>(VP);
				if (SP && SP->Struct && SP->Struct->GetFName() == NAME_Vector)
				{
					void* ValPtr = VP->ContainerPtrToValuePtr<void>(Anim);
					if (ValPtr) *static_cast<FVector*>(ValPtr) = WorldVelocity;
				}
			}
			const bool bIsFalling = WorldVelocity.Z < -1.f;
			static const FName BoolNames[] = { TEXT("bIsFalling"), TEXT("bIsInAir") };
			for (const FName BoolName : BoolNames)
			{
				if (FBoolProperty* BP = CastField<FBoolProperty>(Anim->GetClass()->FindPropertyByName(BoolName)))
				{
					void* ValPtr = BP->ContainerPtrToValuePtr<void>(Anim);
					if (ValPtr) BP->SetPropertyValue(ValPtr, bIsFalling);
				}
			}
		}
	}
}

void AArcaneEntityVisual::SetRunMontagePath(const FSoftObjectPath& MontagePath)
{
	StoredRunMontagePath = MontagePath;
}

void AArcaneEntityVisual::SetClusterColor(FLinearColor Color)
{
	for (UMaterialInstanceDynamic* MI : DynamicMaterials)
	{
		if (MI)
		{
			MI->SetVectorParameterValue(FName("BaseColor"), Color);
			MI->SetVectorParameterValue(FName("Tint"), Color);
		}
	}
}

void AArcaneEntityVisual::SetupSimpleHumanoidFallback()
{
	if (bUsingSimpleFallback) return;
	bUsingSimpleFallback = true;

	UE_LOG(LogArcaneVisual, Log, TEXT("ArcaneEntityVisual: setting up capsule+sphere fallback"));
	MeshComponent->SetVisibility(false);
	MeshComponent->SetSkeletalMesh(nullptr);

	// Attach fallbacks to root (not mesh) so they stay visible when mesh is hidden
	USceneComponent* Root = GetRootComponent();

	// Body: capsule (human proportion), feet at actor origin (Z up)
	const float CapsuleRadius = 40.f;
	const float CapsuleHalfHeight = 55.f;
	FallbackCapsule = NewObject<UCapsuleComponent>(this, TEXT("FallbackBody"));
	FallbackCapsule->SetupAttachment(Root);
	FallbackCapsule->SetCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	FallbackCapsule->SetRelativeLocation(FVector(0.f, 0.f, CapsuleHalfHeight)); // center at half-height so feet at Z=0
	FallbackCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackCapsule->SetVisibility(true);
	FallbackCapsule->SetHiddenInGame(false);
	FallbackCapsule->RegisterComponent();

	// Head: sphere above capsule
	const float HeadRadius = 28.f;
	FallbackHead = NewObject<USphereComponent>(this, TEXT("FallbackHead"));
	FallbackHead->SetupAttachment(Root);
	FallbackHead->SetSphereRadius(HeadRadius);
	FallbackHead->SetRelativeLocation(FVector(0.f, 0.f, CapsuleHalfHeight * 2.f + HeadRadius));
	FallbackHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackHead->SetVisibility(true);
	FallbackHead->SetHiddenInGame(false);
	FallbackHead->RegisterComponent();

	// Single tintable material for both body and head (engine content)
	UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	}
	if (BaseMat)
	{
		UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (MI)
		{
			MI->SetVectorParameterValue(FName("BaseColor"), FLinearColor::Gray);
			DynamicMaterials.Add(MI);
			FallbackCapsule->SetMaterial(0, MI);
			FallbackHead->SetMaterial(0, MI);
		}
	}
}

void AArcaneEntityVisual::SetMeshFromPath(const FSoftObjectPath& MeshPath)
{
	if (MeshPath.IsValid())
	{
		UObject* Obj = MeshPath.TryLoad();
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Obj);
		if (!Mesh && MeshPath.ToString().Contains(TEXT("Quinn")))
		{
			Obj = FSoftObjectPath(TEXT("/Game/Characters/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")).TryLoad();
			Mesh = Cast<USkeletalMesh>(Obj);
		}
		if (!Mesh && MeshPath.ToString().Contains(TEXT("Manny")))
		{
			Obj = FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")).TryLoad();
			Mesh = Cast<USkeletalMesh>(Obj);
		}
		if (Mesh)
		{
			MeshComponent->SetSkeletalMesh(Mesh);
			MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, -96.f));
			MeshComponent->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
			MeshComponent->SetVisibility(true);
			MeshComponent->SetHiddenInGame(false);
			MeshComponent->SetCastShadow(true);
			MeshComponent->SetOwnerNoSee(false);
			MeshComponent->bCastHiddenShadow = false;
			MeshComponent->MarkRenderStateDirty();
			DynamicMaterials.Empty();
			// Use the mesh's default materials (same as player) so entity humanoids match the player character.
			static int32 MeshLoadCount = 0;
			if (++MeshLoadCount <= 1 || MeshLoadCount % 50 == 0)
			{
				UE_LOG(LogArcaneVisual, Log, TEXT("ArcaneEntityVisual: skeletal mesh loaded (default materials), count=%d"), MeshLoadCount);
			}
			return;
		}
	}
	UE_LOG(LogArcaneVisual, Warning, TEXT("ArcaneEntityVisual: mesh failed to load (path=%s), using capsule+sphere fallback"), *MeshPath.ToString());
	SetupSimpleHumanoidFallback();
}

void AArcaneEntityVisual::SetAnimClass(TSubclassOf<UAnimInstance> AnimClass)
{
	if (AnimClass)
	{
		MeshComponent->SetAnimInstanceClass(AnimClass);
	}
}
