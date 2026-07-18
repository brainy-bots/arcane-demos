// Copyright Arcane Engine. Player controller with spectator (free-fly) toggle for demo inspection.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArcaneDemoPlayerController.generated.h"

class ASpectatorPawn;

/**
 * Demo player controller: press Tab (ToggleSpectator action) to detach into a free-fly
 * spectator camera (WASD + Q/E up-down + mouse look, 3x speed) and inspect the world —
 * e.g. watch cluster-colored entities migrate between servers. Press Tab again to
 * return to the character exactly where you left it.
 */
UCLASS()
class ARCANEDEMO_API AArcaneDemoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;

	/** Toggle between the possessed character and a free-fly spectator pawn. */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void ToggleSpectator();

private:
	/** The character we left behind while spectating (repossessed on toggle-back). */
	UPROPERTY()
	TObjectPtr<APawn> SavedPawn;

	/** Live spectator pawn while in free-fly mode. */
	UPROPERTY()
	TObjectPtr<ASpectatorPawn> SpectatorPawnInstance;
};
