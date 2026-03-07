// Copyright Arcane Engine. Replicated bot spawner implementation.

#include "ReplicatedBotSpawner.h"
#include "ArcaneDemoCharacter.h"
#include "ArcaneDemoGameMode.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealBenchmark, Log, All);

AReplicatedBotSpawner::AReplicatedBotSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	BotCharacterClass = AArcaneDemoCharacter::StaticClass();
}

void AReplicatedBotSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) return;

	UWorld* World = GetWorld();
	if (!World || !BotCharacterClass) return;

	int32 N = FMath::Clamp(NumReplicatedBots, 1, 2000);
	if (AArcaneDemoGameMode* GM = Cast<AArcaneDemoGameMode>(UGameplayStatics::GetGameMode(World)))
	{
		N = FMath::Clamp(GM->NumUnrealReplicatedBots, 1, 2000);
	}
	Bots.Reserve(N);

	for (int32 i = 0; i < N; ++i)
	{
		const float Angle = 2.f * PI * (i / float(N)) + FMath::FRand();
		const float R = SpawnRadius * (0.3f + 0.7f * FMath::FRand());
		const FVector Loc = GetActorLocation() + FVector(R * FMath::Cos(Angle), R * FMath::Sin(Angle), 0.f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AArcaneDemoCharacter* Bot = World->SpawnActor<AArcaneDemoCharacter>(BotCharacterClass, Loc, FRotator::ZeroRotator, SpawnParams);
		if (Bot)
		{
			Bot->SetReplicates(true);
			Bot->SetReplicateMovement(true);
			if (Bot->CameraBoom) Bot->CameraBoom->SetVisibility(false);
			if (Bot->FollowCamera) Bot->FollowCamera->SetVisibility(false);
			Bots.Add(Bot);
		}
	}

	TimeUntilNextDirection = 0.5f + FMath::FRand() * 1.5f;
}

void AReplicatedBotSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || Bots.Num() == 0) return;

	// Authority: move bots
	if (HasAuthority())
	{
		TimeUntilNextDirection -= DeltaTime;
		const bool bChangeDirection = TimeUntilNextDirection <= 0.f;
		if (bChangeDirection)
		{
			TimeUntilNextDirection = 1.f + FMath::FRand() * 2.f;
		}

		for (AArcaneDemoCharacter* Bot : Bots)
		{
			if (!Bot || !IsValid(Bot)) continue;
			UCharacterMovementComponent* MoveComp = Bot->GetCharacterMovement();
			if (!MoveComp) continue;

			if (bChangeDirection)
			{
				const float Yaw = FMath::FRand() * 360.f;
				Bot->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			}
			Bot->AddMovementInput(Bot->GetActorForwardVector(), 0.5f + FMath::FRand() * 0.5f);
		}
	}

	// Log FPS + entity count for benchmark (same cadence as Arcane HUD; parseable from log)
	static float LastLogTime = -1.f;
	float Now = World->GetTimeSeconds();
	if (LastLogTime < 0.f) LastLogTime = Now;
	if (Now - LastLogTime >= 0.25f)
	{
		LastLogTime = Now;
		const int32 NumEntities = Bots.Num() + 1; // bots + player
		const float FPS = (DeltaTime > 0.f) ? (1.f / DeltaTime) : 0.f;
		UE_LOG(LogUnrealBenchmark, Log, TEXT("UnrealBenchmark: entities=%d FPS=%.0f"), NumEntities, FPS);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 0.5f, FColor::Silver,
				FString::Printf(TEXT("Entities: %d | FPS: %.0f (Unreal)"), NumEntities, FPS));
		}
		// One-time "demo ready" line for benchmark script (match Arcane format for parsing)
		static bool bLoggedReady = false;
		if (!bLoggedReady && Now >= 2.f)
		{
			bLoggedReady = true;
			UE_LOG(LogUnrealBenchmark, Log, TEXT("Arcane Demo ready (Unreal mode): %d entities visible, replication active."), NumEntities);
		}
	}
}
