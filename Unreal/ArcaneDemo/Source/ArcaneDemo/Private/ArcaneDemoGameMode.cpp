// Copyright Arcane Engine. GameMode implementation.

#include "ArcaneDemoGameMode.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneEntityDisplay.h"
#include "ReplicatedBotSpawner.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AArcaneDemoGameMode::AArcaneDemoGameMode()
{
	// Playable character: move (WASD), jump (Space), third-person camera (mouse look).
	DefaultPawnClass = AArcaneDemoCharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void AArcaneDemoGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Spawn a Player Start if the level has none, so RestartPlayer (called later) can find one.
	EnsurePlayerStartExists();
}

void AArcaneDemoGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Command line overrides for benchmark: -UseUnrealNetworking -ArcaneBenchmarkBots=N
	if (FParse::Param(FCommandLine::Get(), TEXT("UseUnrealNetworking")))
	{
		bUseArcaneNetworking = false;
		FParse::Value(FCommandLine::Get(), TEXT("ArcaneBenchmarkBots="), NumUnrealReplicatedBots);
		NumUnrealReplicatedBots = FMath::Clamp(NumUnrealReplicatedBots, 1, 2000);
	}
	if (bUseArcaneNetworking)
	{
		EnsureEntityDisplayExists();
	}
	else
	{
		EnsureUnrealBotSpawnerExists();
	}
}

void AArcaneDemoGameMode::EnsureEntityDisplayExists()
{
	UWorld* World = GetWorld();
	if (!World) return;

	TArray<AArcaneEntityDisplay*> Displays;
	for (TActorIterator<AArcaneEntityDisplay> It(World); It; ++It)
	{
		Displays.Add(*It);
	}
	// Exactly one display: use it. None: spawn one. More than one (e.g. placed + spawned, or sublevel): keep first, destroy rest so entities aren't duplicated.
	if (Displays.Num() == 0)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AArcaneEntityDisplay>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
	}
	else if (Displays.Num() > 1)
	{
		for (int32 i = 1; i < Displays.Num(); ++i)
		{
			if (Displays[i])
			{
				Displays[i]->Destroy();
			}
		}
	}
}

void AArcaneDemoGameMode::EnsureUnrealBotSpawnerExists()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Remove any Arcane entity display so we don't run both systems.
	for (TActorIterator<AArcaneEntityDisplay> It(World); It; ++It)
	{
		(*It)->Destroy();
	}

	TArray<AReplicatedBotSpawner*> Spawners;
	for (TActorIterator<AReplicatedBotSpawner> It(World); It; ++It)
	{
		Spawners.Add(*It);
	}
	if (Spawners.Num() == 0)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AReplicatedBotSpawner* Spawner = World->SpawnActor<AReplicatedBotSpawner>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (Spawner)
		{
			Spawner->NumReplicatedBots = FMath::Clamp(NumUnrealReplicatedBots, 1, 2000);
		}
	}
	else if (Spawners.Num() > 1)
	{
		for (int32 i = 1; i < Spawners.Num(); ++i)
		{
			if (Spawners[i]) Spawners[i]->Destroy();
		}
	}
}

void AArcaneDemoGameMode::EnsurePlayerStartExists()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bool bFound = false;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		bFound = true;
		break;
	}
	if (!bFound)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<APlayerStart>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, SpawnParams);
	}
}

AActor* AArcaneDemoGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	AActor* Start = Super::ChoosePlayerStart_Implementation(Player);
	if (Start) return Start;

	// Fallback: use first PlayerStart in the world (e.g. one we spawned in InitGame).
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
