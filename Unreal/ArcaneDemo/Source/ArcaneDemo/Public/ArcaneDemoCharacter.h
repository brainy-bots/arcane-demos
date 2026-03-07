// Copyright Arcane Engine. Playable character: move, jump, third-person camera.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArcaneDemoCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * Default playable character for Arcane Demo. Third-person camera, WASD movement, jump,
 * mouse look. Uses ACharacter so you get gravity, walking, and jump out of the box.
 */
UCLASS()
class ARCANEDEMO_API AArcaneDemoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AArcaneDemoCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** When true, velocity is set each frame by Arcane (ApplyEntityStateToActor); movement component must not overwrite it so animations get correct Speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bExternallyDriven = false;

	/** Apply cluster tint to mesh (optional, e.g. for entity color-by-cluster). */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void SetDisplayColor(FLinearColor Color);

	/** Push world velocity to anim instance (Speed, bIsFalling) so blend tree works when not locally controlled. Call after ApplyEntityStateToActor for entity characters. */
	void SetAnimationFromVelocity(FVector WorldVelocity);

	/** Third-person camera boom (arm length, lag). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Follow camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

protected:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
};
