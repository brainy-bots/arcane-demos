// Copyright Arcane Engine. Actor that ticks the Arcane adapter and visualizes entities (spheres or character mesh).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StreamableManager.h"
#include "ArcaneTypes.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneEntityDisplay.generated.h"

class UArcaneAdapterSubsystem;

/**
 * Place in level or spawn from GameMode when using Arcane networking. On BeginPlay calls adapter Connect().
 * Each Tick: adapter Tick(); spawns/updates character per entity via ArcaneAdapterSubsystem::ApplyEntityStateToActor (engine replicated-movement path). Same character class as player; optional cluster color. Set Manager URL on the adapter (e.g. http://127.0.0.1:8081) before Play.
 */
UCLASS()
class ARCANEDEMO_API AArcaneEntityDisplay : public AActor
{
	GENERATED_BODY()

public:
	AArcaneEntityDisplay();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Re-apply Velocity and Acceleration to entity characters from last cached snapshot. Called by LateUpdateComponent in TG_PostUpdateWork so the ABP sees correct values after CMC has ticked. */
	void ReapplyVelocityAndAcceleration();

	/** If true, spawn the same Character class as the player for each entity (mesh + ABP come from that class). If false, use debug spheres only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane|Character")
	bool bUseCharacterMesh = true;

	/** Character class to spawn for each entity (default: same as player). Uses engine replicated-movement path via ArcaneAdapterSubsystem::ApplyEntityStateToActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane|Character")
	TSubclassOf<AArcaneDemoCharacter> EntityCharacterClass;

	/** Radius of body sphere when using debug fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	float EntitySphereRadius = 35.f;

	/** Color of debug spheres when not colorizing by cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	FColor EntityColor = FColor::Green;

	/** If true, auto-connect on BeginPlay using adapter's ManagerUrl. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bAutoConnect = true;

	/**
	 * Scale: server units -> Unreal units (1 UU = 1 cm by default).
	 * Server demo world is 0-5000 (50 m); use 1 for human-scale. When "Center entities on player" is on, we cap to 10.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (ClampMin = "0.01"))
	float PositionScale = 1.f;

	/** Server world center (X and Z). Demo uses 5000-unit world with center 2500; origin = player - Scale * this so server (2500,2500) maps to player. Use 100 for legacy 200-unit world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (ClampMin = "1.0"))
	float ServerWorldCenter = 2500.f;

	/** If false (default): entities are fixed in world; server positions update them so they move. If true: entity cloud follows the player (same relative offset every frame) — use for debugging only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bCenterEntitiesOnPlayer = false;

	/** When not centering on player: world position of server origin (0,0,0). If left at (0,0,0), it is set once to the player position when entities first arrive so the cloud is visible; you can override in editor to fix the cloud elsewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	FVector EntityWorldOrigin = FVector::ZeroVector;

	/** When using character mesh: if true, draw debug spheres (cyan at first entity + colored per-entity) on top of humanoids so you can verify position when humanoids are not visible. Default true for debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bDrawDebugSpheres = true;

	/** If true, color each entity by cluster_id (which server owns it). If false, use EntityColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bColorizeByCluster = true;

	/** If true, send local player position/velocity to cluster each SendIntervalSeconds (so you appear as a moving entity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane")
	bool bSendLocalPlayerState = true;

	/** Interval in seconds between sending player state (~20 Hz = 0.05). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float SendIntervalSeconds = 0.05f;

	/** If > 0, use interpolated snapshot (smoother movement between server ticks). 0.05 = default for smoother demo. 0 = raw snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arcane", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float InterpolationDelaySeconds = 0.05f;

private:
	/** Compute Unreal world position; Offset for center-on-player, ScaleOverride <= 0 means use PositionScale. */
	FVector GetEntityWorldPosition(const FArcaneEntityState& State, const FVector& Offset, float ScaleOverride = -1.f) const;
	void UpdateEntityVisuals(const TArray<FArcaneEntityState>& Snapshot, const FVector& PositionOffset, float ScaleToUse);
	FColor ColorFromClusterId(const FString& ClusterId) const;
	AArcaneDemoCharacter* GetOrSpawnEntityCharacter(const FString& EntityId);

	/** Accumulated time for throttling SendPlayerState. */
	float AccumulatedSendTime = 0.f;
	/** Throttle "connected but no entities" log to every 2s. */
	float NoEntitiesLogTime = 0.f;
	UPROPERTY()
	TWeakObjectPtr<UArcaneAdapterSubsystem> AdapterSubsystem;
	/** Spawned character instances per entity ID (same class as player; we drive position/velocity). */
	UPROPERTY()
	TMap<FString, TObjectPtr<AArcaneDemoCharacter>> EntityCharacters;
	/** When world mode: true after we've set EntityWorldOrigin once from player position (or fallback) so the cloud is visible. */
	bool bWorldOriginPlacedOnce = false;
	/** When we have pawn + snapshot: time we started deferring origin so we set it after player has landed (~2s). */
	float WorldOriginDeferStartTime = -1.f;
	/** When waiting for pawn to set origin: time we first had snapshot but no pawn (for fallback after delay). */
	float WorldOriginFallbackStartTime = -1.f;
	/** Throttle player position log when moving (log at most every 1s). */
	float PlayerPositionLogTime = 0.f;
	/** Last position we logged (so we can log again when player moves). */
	FVector LastLoggedPlayerPos = FVector::ZeroVector;
	/** True after we've logged initial player position at start. */
	bool bLoggedInitialPlayerPos = false;

	/** Cached snapshot/offset/scale from last UpdateEntityVisuals; used by ReapplyVelocityAndAcceleration so ABP sees Velocity/Acceleration after CMC tick. */
	TArray<FArcaneEntityState> CachedEntitySnapshot;
	FVector CachedPositionOffset = FVector::ZeroVector;
	float CachedScale = 1.f;

	UPROPERTY()
	TObjectPtr<class UArcaneEntityDisplayLateUpdateComponent> LateUpdateComponent;
};
