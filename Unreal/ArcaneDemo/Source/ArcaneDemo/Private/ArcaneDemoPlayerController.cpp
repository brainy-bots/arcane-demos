// Copyright Arcane Engine. Spectator toggle implementation.

#include "ArcaneDemoPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneDemoPC, Log, All);

void AArcaneDemoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		InputComponent->BindAction(TEXT("ToggleSpectator"), EInputEvent::IE_Pressed, this,
			&AArcaneDemoPlayerController::ToggleSpectator);
	}
}

void AArcaneDemoPlayerController::ToggleSpectator()
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (SpectatorPawnInstance)
	{
		// Return to the saved character.
		APawn* Back = SavedPawn;
		ASpectatorPawn* Spec = SpectatorPawnInstance;
		SpectatorPawnInstance = nullptr;
		SavedPawn = nullptr;
		if (Back)
		{
			Possess(Back);
		}
		if (Spec)
		{
			Spec->Destroy();
		}
		UE_LOG(LogArcaneDemoPC, Log, TEXT("Spectator OFF (repossessed character)"));
		return;
	}

	// Detach into free-fly: spawn a spectator pawn at the current view location.
	FVector ViewLoc;
	FRotator ViewRot;
	GetPlayerViewPoint(ViewLoc, ViewRot);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASpectatorPawn* Spec =
		World->SpawnActor<ASpectatorPawn>(ASpectatorPawn::StaticClass(), ViewLoc, ViewRot, Params);
	if (!Spec)
	{
		UE_LOG(LogArcaneDemoPC, Warning, TEXT("Spectator pawn spawn failed"));
		return;
	}
	// Faster flying for world-scale inspection.
	if (USpectatorPawnMovement* Move = Cast<USpectatorPawnMovement>(Spec->GetMovementComponent()))
	{
		Move->MaxSpeed = 3600.f;
		Move->Acceleration = 12000.f;
		Move->Deceleration = 14000.f;
	}

	SavedPawn = GetPawn();
	SpectatorPawnInstance = Spec;
	Possess(Spec);
	UE_LOG(LogArcaneDemoPC, Log, TEXT("Spectator ON (free-fly; Tab to return)"));
}
