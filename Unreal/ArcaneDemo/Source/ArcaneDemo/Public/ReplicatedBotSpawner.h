// Copyright Arcane Engine. Spawns replicated characters on the server for "Unreal networking" mode so you can compare performance vs Arcane.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ReplicatedBotSpawner.generated.h"

class AArcaneDemoCharacter;

/**
 * Used when bUseArcaneNetworking is false. Spawns NumReplicatedBots characters on the server
 * and moves them with simple AI so they replicate via default Unreal networking. Run as
 * Listen Server (or Dedicated + Client) to see bots on the client and compare FPS/limits.
 */
UCLASS()
class ARCANEDEMO_API AReplicatedBotSpawner : public AActor
{
	GENERATED_BODY()

public:
	AReplicatedBotSpawner();

	/** Number of replicated bots to spawn (server only). Increase to stress default networking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane|Unreal", meta = (ClampMin = "1", ClampMax = "2000"))
	int32 NumReplicatedBots = 20;

	/** Spawn radius around origin (Unreal units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane|Unreal", meta = (ClampMin = "100"))
	float SpawnRadius = 2000.f;

	/** Character class to spawn (same as player). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane|Unreal")
	TSubclassOf<AArcaneDemoCharacter> BotCharacterClass;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY()
	TArray<TObjectPtr<AArcaneDemoCharacter>> Bots;

	float TimeUntilNextDirection = 0.f;
};
