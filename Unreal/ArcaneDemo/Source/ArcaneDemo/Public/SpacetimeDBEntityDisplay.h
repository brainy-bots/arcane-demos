#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// Required for OnSpacetimeDBConnected signature (UDbConnection, FSpacetimeDBIdentity). Generate with: spacetime generate -l unrealcpp ...
#include "ModuleBindings/SpacetimeDBClient.g.h"
#include "SpacetimeDBEntityDisplay.generated.h"

class UArcaneAdapterSubsystem;
class AArcaneDemoCharacter;

UCLASS()
class ARCANEDEMO_API ASpacetimeDBEntityDisplay : public AActor
{
	GENERATED_BODY()

public:
	ASpacetimeDBEntityDisplay();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacetimeDB")
	FString SpacetimeUri = TEXT("http://localhost:3000");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacetimeDB")
	FString DatabaseName = TEXT("arcane");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacetimeDB", meta = (ClampMin = "0.01"))
	float PositionScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacetimeDB")
	bool bUseCharacterMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacetimeDB")
	TSubclassOf<AArcaneDemoCharacter> EntityCharacterClass;

	/** Called when SpacetimeDB connection is established (used by dynamic delegate). Requires SpacetimeDB bindings. */
	UFUNCTION()
	void OnSpacetimeDBConnected(UDbConnection* InConn, FSpacetimeDBIdentity Identity, const FString& Token);

private:
	void SyncEntitiesFromDb(float DeltaTime);
	UFUNCTION()
	void OnConnectError(const FString& Error);
	AArcaneDemoCharacter* GetOrSpawnEntityCharacter(const FString& EntityId);

	UPROPERTY()
	TObjectPtr<UDbConnection> Connection;
	TWeakObjectPtr<UArcaneAdapterSubsystem> AdapterSubsystem;
	UPROPERTY()
	TMap<FString, TObjectPtr<AArcaneDemoCharacter>> EntityCharacters;
};
