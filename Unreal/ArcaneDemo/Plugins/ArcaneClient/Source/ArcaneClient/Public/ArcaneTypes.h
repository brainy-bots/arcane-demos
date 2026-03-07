// Copyright Arcane Engine. Types used by the Arcane client adapter.

#pragma once

#include "CoreMinimal.h"
#include "ArcaneTypes.generated.h"

/** Single entity state from STATE_UPDATE (matches server EntityStateEntry). ClusterId indicates which server owns this entity (for colorization). */
USTRUCT(BlueprintType)
struct FArcaneEntityState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FString EntityId;

	/** Cluster that owns this entity; use for color-by-cluster (which server is handling the player). */
	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FString ClusterId;

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FVector Velocity = FVector::ZeroVector;

	FArcaneEntityState() = default;
	FArcaneEntityState(const FString& InEntityId, const FString& InClusterId, const FVector& InPosition, const FVector& InVelocity)
		: EntityId(InEntityId), ClusterId(InClusterId), Position(InPosition), Velocity(InVelocity) {}
};

/** Result of GET /join from the manager. */
USTRUCT(BlueprintType)
struct FArcaneJoinResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FString ClusterId;

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FString ServerHost;

	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	int32 ServerPort = 8080;
};
