// Copyright Arcane Engine. Entity display implementation.

#include "ArcaneEntityDisplay.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoCharacterMovementComponent.h"
#include "ArcaneEntityDisplayLateUpdateComponent.h"
#include "ArcaneEntityMovementSyncComponent.h"
#include "ArcaneAdapterSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Containers/Set.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Logging/LogMacros.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneDisplay, Log, All);
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

FColor AArcaneEntityDisplay::ColorFromClusterId(const FString& ClusterId) const
{
	if (ClusterId.IsEmpty()) return FColor::White;
	if (ClusterId.EndsWith(TEXT("440001"))) return FColor(220, 60, 60);
	if (ClusterId.EndsWith(TEXT("440002"))) return FColor(60, 200, 80);
	if (ClusterId.EndsWith(TEXT("440003"))) return FColor(60, 120, 255);
	uint32 Hash = 0;
	for (TCHAR c : ClusterId)
	{
		Hash = (Hash * 31) + static_cast<uint32>(c);
	}
	int32 H = static_cast<int32>(Hash % 360);
	if (H < 0) H += 360;
	float S = 0.8f, V = 1.0f;
	float C = V * S;
	float X = C * (1.0f - FMath::Abs(FMath::Fmod(H / 60.0f, 2.0f) - 1.0f));
	float M = V - C;
	float R = 0, G = 0, B = 0;
	if (H < 60) { R = C; G = X; B = 0; }
	else if (H < 120) { R = X; G = C; B = 0; }
	else if (H < 180) { R = 0; G = C; B = X; }
	else if (H < 240) { R = 0; G = X; B = C; }
	else if (H < 300) { R = X; G = 0; B = C; }
	else { R = C; G = 0; B = X; }
	return FColor(
		FMath::RoundToInt((R + M) * 255),
		FMath::RoundToInt((G + M) * 255),
		FMath::RoundToInt((B + M) * 255),
		255);
}

AArcaneEntityDisplay::AArcaneEntityDisplay()
{
	PrimaryActorTick.bCanEverTick = true;
	// Run early so entity Velocity is set before character components (and thus AnimBlueprint) update this frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	EntityCharacterClass = AArcaneDemoCharacter::StaticClass();
	LateUpdateComponent = CreateDefaultSubobject<UArcaneEntityDisplayLateUpdateComponent>(TEXT("LateUpdateComponent"));
}

void AArcaneEntityDisplay::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay::BeginPlay"));

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		UE_LOG(LogArcaneDisplay, Error, TEXT("ArcaneEntityDisplay: no GameInstance"));
		return;
	}
	AdapterSubsystem = GameInstance->GetSubsystem<UArcaneAdapterSubsystem>();
	if (!AdapterSubsystem.IsValid())
	{
		UE_LOG(LogArcaneDisplay, Error, TEXT("ArcaneEntityDisplay: ArcaneAdapterSubsystem not found (is ArcaneClient plugin enabled?)"));
		return;
	}
	if (bAutoConnect)
	{
		UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: auto-connecting to manager"));
		AdapterSubsystem->Connect();
	}
}

void AArcaneEntityDisplay::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Fallback: if BeginPlay ran before subsystem was ready, bind and connect on first Tick
	if (!AdapterSubsystem.IsValid())
	{
		UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		if (GameInstance)
		{
			AdapterSubsystem = GameInstance->GetSubsystem<UArcaneAdapterSubsystem>();
			if (AdapterSubsystem.IsValid() && bAutoConnect)
			{
				UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: connecting on first Tick (subsystem was not ready in BeginPlay)"));
				AdapterSubsystem->Connect();
			}
		}
		if (!AdapterSubsystem.IsValid()) return;
	}

	AdapterSubsystem->Tick(DeltaTime);

	if (AdapterSubsystem->IsConnected() && bSendLocalPlayerState)
	{
		AccumulatedSendTime += DeltaTime;
		if (AccumulatedSendTime >= SendIntervalSeconds)
		{
			AccumulatedSendTime = 0.f;
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					AdapterSubsystem->SendPlayerState(Pawn->GetActorLocation(), Pawn->GetVelocity());
				}
			}
		}
	}

	// Use raw or interpolated snapshot (InterpolationDelaySeconds > 0 = smoother movement between server ticks)
	TArray<FArcaneEntityState> Snapshot = (InterpolationDelaySeconds > 0.f)
		? AdapterSubsystem->GetInterpolatedEntitySnapshot(InterpolationDelaySeconds)
		: AdapterSubsystem->GetEntitySnapshot();
	UWorld* World = GetWorld();
	if (!World) return;

	// When centering on player, cap scale so the cloud stays visible (server 0-200 -> max 2000 Unreal units spread)
	const float EffectiveScale = bCenterEntitiesOnPlayer ? FMath::Min(PositionScale, 10.f) : PositionScale;
	if (bCenterEntitiesOnPlayer && PositionScale > 10.f)
	{
		static bool bWarnedScale = false;
		if (!bWarnedScale) { UE_LOG(LogArcaneDisplay, Warning, TEXT("ArcaneEntityDisplay: Position Scale %.0f would put entities 20km away; capping to 10 so cloud stays visible. Set Scale to 1-10 for centered view."), PositionScale); bWarnedScale = true; }
	}

	// When not centering: place EntityWorldOrigin at player *after* they have had time to land (~2s), so the entity cloud is at floor height instead of spawn height (avoids "flying" entities).
	if (!bCenterEntitiesOnPlayer && Snapshot.Num() > 0 && !bWorldOriginPlacedOnce)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn)
		{
			float Time = World->GetTimeSeconds();
			if (WorldOriginDeferStartTime < 0.f) WorldOriginDeferStartTime = Time;
			// Wait ~2s so the player has landed (gravity); then lock origin so entities sit on the same height as the player.
			if (Time - WorldOriginDeferStartTime >= 2.f)
			{
				FVector Loc = Pawn->GetActorLocation();
				EntityWorldOrigin = FVector(Loc.X - EffectiveScale * ServerWorldCenter, Loc.Y - EffectiveScale * ServerWorldCenter, Loc.Z);
				bWorldOriginPlacedOnce = true;
				WorldOriginFallbackStartTime = -1.f;
				LastLoggedPlayerPos = Loc;
				UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: entity origin set from player (after 2s): (%.0f, %.0f, %.0f)"), EntityWorldOrigin.X, EntityWorldOrigin.Y, EntityWorldOrigin.Z);
			}
		}
		else
		{
			// Snapshot arrived before pawn was ready; after 0.5s use fallback. Entity Z = EntityWorldOrigin.Z + GroundZ + S*serverY, so we set origin Z=0 and you set GroundZ to your floor Z (e.g. -226 for top of plane at center -230).
			float Time = World->GetTimeSeconds();
			if (WorldOriginFallbackStartTime < 0.f) WorldOriginFallbackStartTime = Time;
			if (Time - WorldOriginFallbackStartTime >= 0.5f)
			{
				EntityWorldOrigin = FVector(-EffectiveScale * ServerWorldCenter, -EffectiveScale * ServerWorldCenter, 0.f);
				bWorldOriginPlacedOnce = true;
				UE_LOG(LogArcaneDisplay, Warning, TEXT("ArcaneEntityDisplay: entity origin set from fallback (pawn not ready). Set EntityWorldOrigin in editor if entities are at wrong height."));
			}
		}
	}

	// Log player position at start and when they move (throttled: every 1s or when moved > 50 units)
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			FVector Loc = Pawn->GetActorLocation();
			float Time = World->GetTimeSeconds();
			float Dist = (Loc - LastLoggedPlayerPos).Size();
			bool bShouldLog = false;
			if (!bLoggedInitialPlayerPos)
			{
				bShouldLog = true;
				bLoggedInitialPlayerPos = true;
				UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: player position at start: (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z);
			}
			else if ((Time - PlayerPositionLogTime >= 1.f) || (Dist > 50.f))
			{
				bShouldLog = true;
				UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: player position (moved): (%.0f, %.0f, %.0f)"), Loc.X, Loc.Y, Loc.Z);
			}
			if (bShouldLog)
			{
				LastLoggedPlayerPos = Loc;
				PlayerPositionLogTime = Time;
			}
		}
	}

	// Offset: when centering on player, cloud center at player X,Y each frame (entities appear to follow the player).
	// For world-fixed entities that move from server updates, keep bCenterEntitiesOnPlayer = false (default).
	// During the first 2s before we lock the origin, follow the player's position so the cloud is visible and then locks at landed height.
	FVector PositionOffset = EntityWorldOrigin;
	if (!bCenterEntitiesOnPlayer && Snapshot.Num() > 0 && !bWorldOriginPlacedOnce && WorldOriginDeferStartTime >= 0.f)
	{
		float Time = World->GetTimeSeconds();
		if (Time - WorldOriginDeferStartTime < 2.f)
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					FVector Loc = Pawn->GetActorLocation();
					PositionOffset = FVector(Loc.X - EffectiveScale * ServerWorldCenter, Loc.Y - EffectiveScale * ServerWorldCenter, Loc.Z);
				}
			}
		}
	}
	if (bCenterEntitiesOnPlayer && Snapshot.Num() > 0)
	{
		static bool bWarnedCenter = false;
		if (!bWarnedCenter)
		{
			UE_LOG(LogArcaneDisplay, Warning, TEXT("ArcaneEntityDisplay: bCenterEntitiesOnPlayer is true — entity cloud follows the player. Set to false for world-fixed entities that move from server."));
			bWarnedCenter = true;
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				FVector Loc = Pawn->GetActorLocation();
				PositionOffset = FVector(Loc.X - EffectiveScale * ServerWorldCenter, Loc.Y - EffectiveScale * ServerWorldCenter, EntityWorldOrigin.Z);
			}
		}
	}

	if (AdapterSubsystem->IsConnected() && Snapshot.Num() == 0)
	{
		NoEntitiesLogTime += DeltaTime;
		if (NoEntitiesLogTime >= 2.f)
		{
			UE_LOG(LogArcaneDisplay, Warning, TEXT("ArcaneEntityDisplay: connected but received 0 entities. Is the cluster running? Check manager URL and cluster DEMO_ENTITIES."));
			NoEntitiesLogTime = 0.f;
		}
	}
	else
	{
		NoEntitiesLogTime = 0.f;
	}

	// Periodic diagnostic log (every 2s) when we have entities: confirms display gets snapshot and shows positions
	if (Snapshot.Num() > 0)
	{
		static float LastDiagnosticTime = -1.f;
		static bool bLoggedDemoReady = false;
		float Time = World->GetTimeSeconds();
		if (LastDiagnosticTime < 0.f || (Time - LastDiagnosticTime) >= 2.f)
		{
			LastDiagnosticTime = Time;
			const FArcaneEntityState& First = Snapshot[0];
			FVector FirstWorld = GetEntityWorldPosition(First, PositionOffset, EffectiveScale);
			float FPS = (DeltaTime > 0.f) ? (1.f / DeltaTime) : 0.f;
			UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: snapshot=%d useMesh=%d scale=%.2f (effective=%.2f) offset=(%.0f,%.0f,%.0f) first serverPos=(%.1f,%.1f,%.1f) first worldPos=(%.0f,%.0f,%.0f) visuals=%d FPS=%.0f"),
				Snapshot.Num(), bUseCharacterMesh ? 1 : 0, PositionScale, EffectiveScale,
				PositionOffset.X, PositionOffset.Y, PositionOffset.Z,
				First.Position.X, First.Position.Y, First.Position.Z,
				FirstWorld.X, FirstWorld.Y, FirstWorld.Z, EntityCharacters.Num(), FPS);
			// MovementCheck: log first and mid entity server pos so verification can confirm positions change over time
			if (Snapshot.Num() > 1)
			{
				const FArcaneEntityState& Other = Snapshot[Snapshot.Num() / 2];
				UE_LOG(LogArcaneDisplay, Log, TEXT("MovementCheck: gameTime=%.1f first serverPos=(%.2f,%.2f,%.2f) mid serverPos=(%.2f,%.2f,%.2f)"),
					Time, First.Position.X, First.Position.Y, First.Position.Z,
					Other.Position.X, Other.Position.Y, Other.Position.Z);
			}
			// Server X,Y = horizontal, Z = vertical. Log ranges to confirm 2D spread (not a line).
			float MinX = First.Position.X, MaxX = First.Position.X;
			float MinY = First.Position.Y, MaxY = First.Position.Y;
			float MinZ = First.Position.Z, MaxZ = First.Position.Z;
			for (const FArcaneEntityState& S : Snapshot)
			{
				MinX = FMath::Min(MinX, S.Position.X); MaxX = FMath::Max(MaxX, S.Position.X);
				MinY = FMath::Min(MinY, S.Position.Y); MaxY = FMath::Max(MaxY, S.Position.Y);
				MinZ = FMath::Min(MinZ, S.Position.Z); MaxZ = FMath::Max(MaxZ, S.Position.Z);
			}
			UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: server X range %.1f..%.1f Y range %.1f..%.1f Z (height) %.2f..%.2f (X,Y should both span ~0..200)"), MinX, MaxX, MinY, MaxY, MinZ, MaxZ);
			// Distinct cluster count for multi-cluster verification (Step 1)
			TSet<FString> UniqueClusterIds;
			for (const FArcaneEntityState& S : Snapshot) { UniqueClusterIds.Add(S.ClusterId); }
			const int32 NumClusters = UniqueClusterIds.Num();
			// One-time "demo ready" when replication and visuals are in sync (for verification and show)
			if (!bLoggedDemoReady && EntityCharacters.Num() >= (int32)Snapshot.Num() && Snapshot.Num() > 0)
			{
				bLoggedDemoReady = true;
				if (NumClusters > 1)
				{
					UE_LOG(LogArcaneDisplay, Log, TEXT("Arcane Demo ready: %d entities from %d clusters visible, replication active."), Snapshot.Num(), NumClusters);
				}
				else
				{
					UE_LOG(LogArcaneDisplay, Log, TEXT("Arcane Demo ready: %d entities visible, replication active."), Snapshot.Num());
				}
			}
		}
		// On-screen HUD: entity count (+ cluster count when multi-cluster) + FPS
		{
			static float LastHudTime = -1.f;
			float Now = World->GetTimeSeconds();
			if (LastHudTime < 0.f || (Now - LastHudTime) >= 0.25f)
			{
				LastHudTime = Now;
				float FPS = (DeltaTime > 0.f) ? (1.f / DeltaTime) : 0.f;
				if (GEngine)
				{
					TSet<FString> ClusterIds;
					for (const FArcaneEntityState& S : Snapshot) { ClusterIds.Add(S.ClusterId); }
					const int32 NumClusters = ClusterIds.Num();
					if (NumClusters > 1)
					{
						GEngine->AddOnScreenDebugMessage(0, 0.5f, FColor::White,
							FString::Printf(TEXT("Entities: %d | Clusters: %d | FPS: %.0f"), Snapshot.Num(), NumClusters, FPS));
					}
					else
					{
						GEngine->AddOnScreenDebugMessage(0, 0.5f, FColor::White,
							FString::Printf(TEXT("Entities: %d | FPS: %.0f"), Snapshot.Num(), FPS));
					}
				}
			}
		}
		// Always draw one debug sphere at first entity so you can confirm placement (even if mesh/fallback don't render)
		{
			FVector FirstWorld = GetEntityWorldPosition(Snapshot[0], PositionOffset, EffectiveScale);
			DrawDebugSphere(World, FirstWorld, 50.f, 8, FColor::Cyan, false, 0.f, 0, 2.f);
		}
		if (bDrawDebugSpheres)
		{
			FVector FirstWorld = GetEntityWorldPosition(Snapshot[0], PositionOffset, EffectiveScale);
			DrawDebugSphere(World, FirstWorld, 80.f, 12, FColor::Cyan, false, 0.f, 0, 3.f);
		}
	}

	// Use entity visuals (skeletal mesh + same anim as player when available) whenever bUseCharacterMesh is true
	if (bUseCharacterMesh)
	{
		UpdateEntityVisuals(Snapshot, PositionOffset, EffectiveScale);
		if (bDrawDebugSpheres)
		{
			for (const FArcaneEntityState& State : Snapshot)
			{
				FVector WorldPos = GetEntityWorldPosition(State, PositionOffset, EffectiveScale);
				FColor Color = bColorizeByCluster ? ColorFromClusterId(State.ClusterId) : EntityColor;
				DrawDebugSphere(World, WorldPos, 25.f, 8, Color, false, 0.f, 0, 1.5f);
			}
		}
	}
	else
	{
		const float BodyRadius = 35.f;
		const float HeadRadius = 18.f;
		const float HeadOffsetZ = BodyRadius + HeadRadius;
		for (const FArcaneEntityState& State : Snapshot)
		{
			FVector WorldPos = GetEntityWorldPosition(State, PositionOffset, EffectiveScale);
			FColor Color = bColorizeByCluster ? ColorFromClusterId(State.ClusterId) : EntityColor;
			DrawDebugSphere(World, WorldPos, BodyRadius, 12, Color, false, -1.f, 0, 2.f);
			DrawDebugSphere(World, WorldPos + FVector(0.f, 0.f, HeadOffsetZ), HeadRadius, 12, Color, false, -1.f, 0, 2.f);
		}
	}
}

FVector AArcaneEntityDisplay::GetEntityWorldPosition(const FArcaneEntityState& State, const FVector& Offset, float ScaleOverride) const
{
	const float S = (ScaleOverride > 0.f) ? ScaleOverride : PositionScale;
	return Offset + S * State.Position;
}

void AArcaneEntityDisplay::UpdateEntityVisuals(const TArray<FArcaneEntityState>& Snapshot, const FVector& PositionOffset, float ScaleToUse)
{
	// Cache for ReapplyVelocityAndAcceleration (runs in PostUpdateWork so ABP sees Velocity/Acceleration after CMC tick).
	CachedEntitySnapshot = Snapshot;
	CachedPositionOffset = PositionOffset;
	CachedScale = ScaleToUse;

	TSet<FString> CurrentIds;
	for (const FArcaneEntityState& State : Snapshot)
	{
		CurrentIds.Add(State.EntityId);
		FColor Color = bColorizeByCluster ? ColorFromClusterId(State.ClusterId) : EntityColor;

		AArcaneDemoCharacter* Char = GetOrSpawnEntityCharacter(State.EntityId);
		if (Char && AdapterSubsystem.IsValid())
		{
			AdapterSubsystem->ApplyEntityStateToActor(Char, State, PositionOffset, ScaleToUse);
			const FVector WorldVel = ScaleToUse * State.Velocity;
			// So ABP sees Velocity/Acceleration after CMC ticks: sync component ticks after CMC, before mesh.
			if (UArcaneEntityMovementSyncComponent* Sync = Char->FindComponentByClass<UArcaneEntityMovementSyncComponent>())
			{
				Sync->PendingVelocity = WorldVel;
			}
			else
			{
				if (UArcaneDemoCharacterMovementComponent* DemoMC = Cast<UArcaneDemoCharacterMovementComponent>(Char->GetCharacterMovement()))
				{
					DemoMC->SetAccelerationForAnimation(WorldVel);
				}
			}
			Char->SetDisplayColor(FLinearColor(Color));
		}
	}

	TArray<FString> ToRemove;
	for (const auto& Pair : EntityCharacters)
	{
		if (!CurrentIds.Contains(Pair.Key))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Id : ToRemove)
	{
		if (AArcaneDemoCharacter* Char = EntityCharacters.FindRef(Id))
		{
			Char->Destroy();
		}
		EntityCharacters.Remove(Id);
	}
}

void AArcaneEntityDisplay::ReapplyVelocityAndAcceleration()
{
	if (CachedScale <= 0.f || CachedEntitySnapshot.Num() == 0) return;
	for (const FArcaneEntityState& State : CachedEntitySnapshot)
	{
		if (AArcaneDemoCharacter* Char = EntityCharacters.FindRef(State.EntityId))
		{
			const FVector WorldVel = CachedScale * State.Velocity;
			if (UCharacterMovementComponent* MC = Char->GetCharacterMovement())
			{
				MC->Velocity = WorldVel;
				if (UArcaneDemoCharacterMovementComponent* DemoMC = Cast<UArcaneDemoCharacterMovementComponent>(MC))
				{
					DemoMC->SetAccelerationForAnimation(WorldVel);
				}
			}
		}
	}
}

AArcaneDemoCharacter* AArcaneEntityDisplay::GetOrSpawnEntityCharacter(const FString& EntityId)
{
	if (TObjectPtr<AArcaneDemoCharacter>* Existing = EntityCharacters.Find(EntityId))
	{
		if (*Existing)
		{
			return *Existing;
		}
		EntityCharacters.Remove(EntityId);
	}

	UWorld* World = GetWorld();
	if (!World || !EntityCharacterClass) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArcaneDemoCharacter* Char = World->SpawnActor<AArcaneDemoCharacter>(EntityCharacterClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Char) return nullptr;

	const int32 Total = EntityCharacters.Num() + 1;
	if (Total <= 1 || Total % 50 == 0)
	{
		UE_LOG(LogArcaneDisplay, Log, TEXT("ArcaneEntityDisplay: spawned entity character (total=%d)"), Total);
	}

	Char->SetActorHiddenInGame(false);
	Char->bExternallyDriven = true; // So movement component doesn't overwrite velocity; ABP gets correct Speed for animations
	Char->SetReplicatingMovement(true);
	// Disable CMC tick so it never overwrites Velocity/Acceleration; we set position and velocity every frame. ABP then reads correct values.
	if (UCharacterMovementComponent* MC = Char->GetCharacterMovement())
	{
		MC->PrimaryComponentTick.bCanEverTick = false;
	}
	// Ensure we tick before this character so Velocity/Acceleration are set before its AnimBlueprint reads them this frame.
	Char->PrimaryActorTick.AddPrerequisite(this, this->PrimaryActorTick);
	// Hide camera on entity (not possessed).
	if (Char->CameraBoom) Char->CameraBoom->SetVisibility(false);
	if (Char->FollowCamera) Char->FollowCamera->SetVisibility(false);

	// Entity must use an AnimBlueprint that sets Speed from GetVelocity() for all pawns, not only when "Is Locally Controlled".
	// ABP_ArcaneEntity: duplicate of ABP_Unarmed with Event Graph always set Speed = GetVelocity().Size().
	// Try correct path first, then duplicated folder (Characters/Characters/...) from template/migration.
	static const TCHAR* EntityABPPaths[] = {
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Characters/Mannequins/Animations/Unarmed/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Characters/Characters/Mannequins/Anims/Unarmed/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Characters/Characters/Mannequins/Animations/Unarmed/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Animations/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Characters/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		TEXT("/Game/Anims/Unarmed/ABP_ArcaneEntity.ABP_ArcaneEntity"),
		nullptr
	};
	UClass* EntityAnimClass = nullptr;
	for (int i = 0; EntityABPPaths[i]; ++i)
	{
		UBlueprint* ABP = LoadObject<UBlueprint>(nullptr, EntityABPPaths[i]);
		if (ABP && ABP->GeneratedClass)
		{
			EntityAnimClass = ABP->GeneratedClass;
			break;
		}
	}
	if (!EntityAnimClass)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogArcaneDisplay, Error, TEXT("ABP_ArcaneEntity not found. Entity animations will not play. Create it (see README 'Entity animations') — no fallback."));
		}
		// No fallback: mesh keeps character default so the missing asset is obvious.
	}
	else
	{
		static bool bLoggedABPUsed = false;
		if (!bLoggedABPUsed)
		{
			bLoggedABPUsed = true;
			UE_LOG(LogArcaneDisplay, Log, TEXT("Entity animations: using ABP_ArcaneEntity (Should Move -> Walk/Run transition will use Velocity + Acceleration from movement component)."));
		}
		if (USkeletalMeshComponent* Mesh = Char->GetMesh())
		{
			Mesh->SetAnimInstanceClass(EntityAnimClass);
		}
	}

	// Sync component: ticks after CMC, before mesh, so ABP reads correct Velocity/Acceleration.
	UArcaneEntityMovementSyncComponent* Sync = NewObject<UArcaneEntityMovementSyncComponent>(Char, TEXT("ArcaneEntityMovementSync"));
	if (Sync)
	{
		Sync->RegisterComponent();
		Sync->SetTickOrderAfterMovementAndBeforeMesh();
	}

	EntityCharacters.Add(EntityId, Char);
	return Char;
}
