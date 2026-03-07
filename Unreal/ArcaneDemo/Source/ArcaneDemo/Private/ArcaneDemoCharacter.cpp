// Copyright Arcane Engine. Playable character implementation.

#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoCharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Logging/LogMacros.h"
#include "Animation/AnimInstance.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneDemoCharacter, Log, All);

AArcaneDemoCharacter::AArcaneDemoCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UArcaneDemoCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates; only the camera does
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
		MoveComp->JumpZVelocity = 400.f;
		MoveComp->AirControl = 0.2f;
	}

	// Camera boom: arm behind the character
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 60.f));

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Skeletal mesh is required: load from project Content (see Content/CHARACTER_SETUP.md).
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (SkelMesh)
	{
		SkelMesh->SetRelativeLocation(FVector(0.f, 0.f, -96.f));
		SkelMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		// Intro project uses SKM_Quinn_Simple / SKM_Manny_Simple. Try both folder structures.
		static const TCHAR* MeshPaths[] = {
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
			TEXT("/Game/Characters/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
			TEXT("/Game/Characters/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		};
		USkeletalMesh* LoadedMesh = nullptr;
		for (const TCHAR* Path : MeshPaths)
		{
			LoadedMesh = LoadObject<USkeletalMesh>(nullptr, Path);
			if (LoadedMesh) break;
		}
		if (LoadedMesh)
		{
			SkelMesh->SetSkeletalMesh(LoadedMesh);
			SkelMesh->SetVisibility(true);
			UBlueprint* ABP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed"));
			if (ABP && ABP->GeneratedClass)
			{
				SkelMesh->SetAnimInstanceClass(ABP->GeneratedClass);
			}
			else
			{
				UE_LOG(LogArcaneDemoCharacter, Error,
					TEXT("ABP_Unarmed failed to load or has compile errors. Open Content/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed, fix red nodes, Compile and Save."));
			}
		}
		else
		{
			UE_LOG(LogArcaneDemoCharacter, Error,
				TEXT("ArcaneDemoCharacter: No skeletal mesh. One-time: run Scripts\\Copy-CharacterFromTemplate.ps1 -SourceProject <ThirdPersonProjectPath>"));
		}
	}
}

void AArcaneDemoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent) return;

	// Movement
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AArcaneDemoCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AArcaneDemoCharacter::MoveRight);
	// Look (turn camera / character)
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AArcaneDemoCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AArcaneDemoCharacter::LookUp);
	// Jump
	PlayerInputComponent->BindAction(TEXT("Jump"), EInputEvent::IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), EInputEvent::IE_Released, this, &ACharacter::StopJumping);
}

void AArcaneDemoCharacter::MoveForward(float Value)
{
	if (Controller && FMath::Abs(Value) > 0.0001f)
	{
		const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
		const FVector Direction = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AArcaneDemoCharacter::MoveRight(float Value)
{
	if (Controller && FMath::Abs(Value) > 0.0001f)
	{
		const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
		const FVector Direction = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
		AddMovementInput(Direction, Value);
	}
}

void AArcaneDemoCharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void AArcaneDemoCharacter::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void AArcaneDemoCharacter::SetAnimationFromVelocity(FVector WorldVelocity)
{
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) return;

	const float Speed = WorldVelocity.Size();
	const bool bIsFalling = WorldVelocity.Z < -1.f;

	static const FName SpeedNames[] = { TEXT("Speed"), TEXT("GroundSpeed"), TEXT("MovementSpeed") };
	for (const FName& Name : SpeedNames)
	{
		if (FProperty* P = Anim->GetClass()->FindPropertyByName(Name))
		{
			if (FFloatProperty* FProp = CastField<FFloatProperty>(P))
			{
				void* ValPtr = P->ContainerPtrToValuePtr<void>(Anim);
				if (ValPtr) { FProp->SetFloatingPointPropertyValue(ValPtr, Speed); break; }
			}
		}
	}
	if (FProperty* VP = Anim->GetClass()->FindPropertyByName(TEXT("Velocity")))
	{
		FStructProperty* SP = CastField<FStructProperty>(VP);
		if (SP && SP->Struct && SP->Struct->GetFName() == NAME_Vector)
		{
			void* ValPtr = VP->ContainerPtrToValuePtr<void>(Anim);
			if (ValPtr) *static_cast<FVector*>(ValPtr) = WorldVelocity;
		}
	}
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

void AArcaneDemoCharacter::SetDisplayColor(FLinearColor Color)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;
	for (int32 i = 0; i < SkelMesh->GetNumMaterials(); ++i)
	{
		UMaterialInterface* Mat = SkelMesh->GetMaterial(i);
		if (Mat)
		{
			UMaterialInstanceDynamic* MI = SkelMesh->CreateAndSetMaterialInstanceDynamic(i);
			if (MI)
			{
				MI->SetVectorParameterValue(FName("BaseColor"), Color);
				MI->SetVectorParameterValue(FName("Tint"), Color);
			}
		}
	}
}
