#include "SpacetimeDBEntityDisplay.h"
#include "ArcaneAdapterSubsystem.h"
#include "ArcaneTypes.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoCharacterMovementComponent.h"
#include "ArcaneEntityMovementSyncComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Logging/LogMacros.h"

#if __has_include("ModuleBindings/SpacetimeDBClient.g.h")
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "ModuleBindings/Tables/EntityTable.g.h"
#define HAS_SPACETIMEDB_BINDINGS 1
#endif
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpacetimeDBDisplay, Log, All);

ASpacetimeDBEntityDisplay::ASpacetimeDBEntityDisplay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void ASpacetimeDBEntityDisplay::BeginPlay()
{
	Super::BeginPlay();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		UE_LOG(LogSpacetimeDBDisplay, Error, TEXT("SpacetimeDBEntityDisplay: no GameInstance"));
		return;
	}
	AdapterSubsystem = GameInstance->GetSubsystem<UArcaneAdapterSubsystem>();
	if (!AdapterSubsystem.IsValid())
	{
		UE_LOG(LogSpacetimeDBDisplay, Error, TEXT("SpacetimeDBEntityDisplay: ArcaneAdapterSubsystem not found"));
		return;
	}

#if HAS_SPACETIMEDB_BINDINGS
	UDbConnectionBuilder* Builder = UDbConnection::Builder();
	if (!Builder)
	{
		UE_LOG(LogSpacetimeDBDisplay, Error, TEXT("SpacetimeDBEntityDisplay: Builder() failed"));
		return;
	}
	FOnConnectDelegate OnConnectDel;
	OnConnectDel.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(ASpacetimeDBEntityDisplay, OnSpacetimeDBConnected));
	FOnConnectErrorDelegate OnErrorDel;
	OnErrorDel.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(ASpacetimeDBEntityDisplay, OnConnectError));
	// Current SDK exposes WithDatabaseName (per generated SpacetimeDBClient.g.h)
	Builder->WithUri(SpacetimeUri)->WithDatabaseName(DatabaseName)->OnConnect(OnConnectDel)->OnConnectError(OnErrorDel);
	Connection = Builder->Build();
	if (!Connection)
	{
		UE_LOG(LogSpacetimeDBDisplay, Error, TEXT("SpacetimeDBEntityDisplay: Build() failed"));
	}
#else
	UE_LOG(LogSpacetimeDBDisplay, Warning, TEXT("SpacetimeDBEntityDisplay: ModuleBindings/SpacetimeDBClient.g.h not found. Run spacetime generate --lang unrealcpp for your module. Stub mode (0 entities)."));
#endif
}

#if HAS_SPACETIMEDB_BINDINGS
void ASpacetimeDBEntityDisplay::OnSpacetimeDBConnected(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token)
{
	if (InConn) Connection = InConn;
	if (InConn && InConn->Db) InConn->Db->Initialize();
	USubscriptionBuilder* Sub = InConn ? InConn->SubscriptionBuilder() : nullptr;
	if (Sub) Sub->SubscribeToAllTables();
	UE_LOG(LogSpacetimeDBDisplay, Log, TEXT("SpacetimeDBEntityDisplay: connected"));
}
#endif

void ASpacetimeDBEntityDisplay::OnConnectError(const FString& Error)
{
	UE_LOG(LogSpacetimeDBDisplay, Warning, TEXT("SpacetimeDBEntityDisplay: connect error %s"), *Error);
}

void ASpacetimeDBEntityDisplay::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if HAS_SPACETIMEDB_BINDINGS
	if (Connection && Connection->IsActive())
	{
		Connection->FrameTick();
		SyncEntitiesFromDb(DeltaTime);
	}
#else
	static bool bLogged = false;
	if (!bLogged) { bLogged = true; UE_LOG(LogSpacetimeDBDisplay, Log, TEXT("Arcane Demo ready: 0 entities visible (SpacetimeDB stub; run codegen for full display).")); }
#endif
}

#if HAS_SPACETIMEDB_BINDINGS
void ASpacetimeDBEntityDisplay::SyncEntitiesFromDb(float DeltaTime)
{
	if (!Connection || !Connection->Db) return;
	UEntityTable* EntityTable = Connection->Db->Entity;
	if (!EntityTable || !AdapterSubsystem.IsValid()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	TArray<FEntityType> Rows = EntityTable->Iter();
	if (Rows.Num() == 0)
	{
		static double LastEmptyLog = -1.0;
		double Now = World->GetTimeSeconds();
		if (LastEmptyLog < 0.0 || Now - LastEmptyLog >= 5.0)
		{
			LastEmptyLog = Now;
			UE_LOG(LogSpacetimeDBDisplay, Warning, TEXT("SpacetimeDB Entity table empty. Is the simulator running?"));
		}
		return;
	}
	TSet<FString> CurrentIds;
	for (const FEntityType& Row : Rows)
	{
		FString IdStr = Row.EntityId.ToString(EGuidFormats::DigitsWithHyphens);
		CurrentIds.Add(IdStr);
		FVector Position(static_cast<float>(Row.X), static_cast<float>(Row.Z), static_cast<float>(Row.Y));
		FVector Velocity(static_cast<float>(Row.Vx), static_cast<float>(Row.Vz), static_cast<float>(Row.Vy));
		FArcaneEntityState State(IdStr, TEXT("spacetime"), Position, Velocity);
		AArcaneDemoCharacter* Char = GetOrSpawnEntityCharacter(IdStr);
		if (Char && AdapterSubsystem.IsValid())
		{
			AdapterSubsystem->ApplyEntityStateToActor(Char, State, FVector::ZeroVector, PositionScale);
		}
	}

	TArray<FString> ToRemove;
	for (const auto& Pair : EntityCharacters)
	{
		if (!CurrentIds.Contains(Pair.Key)) ToRemove.Add(Pair.Key);
	}
	for (const FString& Id : ToRemove)
	{
		if (AArcaneDemoCharacter* Char = EntityCharacters.FindRef(Id))
		{
			Char->Destroy();
		}
		EntityCharacters.Remove(Id);
	}

	static float LastLog = -1.f;
	if (Rows.Num() > 0 && (LastLog < 0.f || World->GetTimeSeconds() - LastLog >= 2.f))
	{
		LastLog = World->GetTimeSeconds();
		float FPS = (DeltaTime > 0.f) ? (1.f / DeltaTime) : 0.f;
		UE_LOG(LogSpacetimeDBDisplay, Log, TEXT("Arcane Demo ready: %d entities visible, replication active. FPS=%.0f"), Rows.Num(), FPS);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(0, 0.5f, FColor::White, FString::Printf(TEXT("Entities: %d | FPS: %.0f"), Rows.Num(), FPS));
		}
	}
}
#endif

#if !HAS_SPACETIMEDB_BINDINGS
void ASpacetimeDBEntityDisplay::SyncEntitiesFromDb(float DeltaTime)
{
}
#endif

AArcaneDemoCharacter* ASpacetimeDBEntityDisplay::GetOrSpawnEntityCharacter(const FString& EntityId)
{
	if (TObjectPtr<AArcaneDemoCharacter>* Existing = EntityCharacters.Find(EntityId))
	{
		if (*Existing) return *Existing;
		EntityCharacters.Remove(EntityId);
	}
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	UClass* ClassToUse = EntityCharacterClass ? EntityCharacterClass.Get() : AArcaneDemoCharacter::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArcaneDemoCharacter* Char = World->SpawnActor<AArcaneDemoCharacter>(ClassToUse, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Char) return nullptr;
	Char->SetActorHiddenInGame(false);
	Char->bExternallyDriven = true;
	Char->SetReplicatingMovement(true);
	if (UCharacterMovementComponent* MC = Char->GetCharacterMovement())
	{
		MC->PrimaryComponentTick.bCanEverTick = false;
	}
	Char->PrimaryActorTick.AddPrerequisite(this, this->PrimaryActorTick);
	if (Char->CameraBoom) Char->CameraBoom->SetVisibility(false);
	if (Char->FollowCamera) Char->FollowCamera->SetVisibility(false);
	UArcaneEntityMovementSyncComponent* Sync = NewObject<UArcaneEntityMovementSyncComponent>(Char, TEXT("ArcaneEntityMovementSync"));
	if (Sync) { Sync->RegisterComponent(); Sync->SetTickOrderAfterMovementAndBeforeMesh(); }
	EntityCharacters.Add(EntityId, Char);
	return Char;
}
