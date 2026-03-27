// Copyright Arcane Engine. GameMode that ensures ArcaneEntityDisplay exists when playing.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ArcaneDemoGameMode.generated.h"

class AReplicatedBotSpawner;
class ASpacetimeDBEntityDisplay;

/**
 * Default game mode for Arcane Demo. Switch networking mode to compare default Unreal replication vs Arcane.
 * - Use Arcane networking (default): spawns ArcaneEntityDisplay, entities from Rust backend via ApplyEntityStateToActor.
 * - Use Unreal networking: spawns ReplicatedBotSpawner on server; run as Listen Server to see replicated bots and compare performance.
 */
UCLASS()
class ARCANEDEMO_API AArcaneDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AArcaneDemoGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;

	/** If no Player Start exists, return the first one in the world (we spawn one in InitGame). */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** If true (default): use Arcane backend + ApplyEntityStateToActor. If false: use default Unreal replication (spawn ReplicatedBotSpawner on server; run as Listen Server to compare). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bUseArcaneNetworking = true;

	/** When bUseArcaneNetworking is false and bUseSpacetimeDBNetworking is false: number of replicated bots to spawn on server for stress test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (EditCondition = "!bUseArcaneNetworking && !bUseSpacetimeDBNetworking", ClampMin = "1", ClampMax = "2000"))
	int32 NumUnrealReplicatedBots = 20;

	/** When bUseArcaneNetworking is false: if true use SpacetimeDB (subscribe to Entity table); if false use default Unreal replication. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (EditCondition = "!bUseArcaneNetworking"))
	bool bUseSpacetimeDBNetworking = false;

private:
	void EnsureEntityDisplayExists();
	void EnsureUnrealBotSpawnerExists();
	void EnsureSpacetimeDBEntityDisplayExists();
	/** If the level has no Player Start, spawn one so the player can be created. */
	void EnsurePlayerStartExists();
};
