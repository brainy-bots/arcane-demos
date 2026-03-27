// Copyright Arcane Engine. Adapter subsystem: HTTP join to manager, WebSocket to cluster, entity cache.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArcaneConnectionClient.h"
#include "ArcaneEntityCache.h"
#include "ArcaneTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ArcaneAdapterSubsystem.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FArcaneOnConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArcaneOnDisconnected, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArcaneOnConnectionFailed, const FString&, Reason);

UENUM(BlueprintType)
enum class EArcaneConnectionState : uint8
{
	Disconnected UMETA(DisplayName = "Disconnected"),
	Joining UMETA(DisplayName = "Joining"),
	ConnectingWebSocket UMETA(DisplayName = "ConnectingWebSocket"),
	Connected UMETA(DisplayName = "Connected"),
	Reconnecting UMETA(DisplayName = "Reconnecting"),
	Failed UMETA(DisplayName = "Failed")
};

/**
 * Game instance subsystem that implements the Arcane client adapter.
 * - Initialize(ManagerUrl): set manager base URL (e.g. http://127.0.0.1:8081).
 * - Connect(): HTTP GET ManagerUrl/join, then WebSocket to returned host:port; receives STATE_UPDATE JSON.
 * - Tick(): call from game thread each frame; drains WebSocket messages and updates entity cache.
 * - GetEntitySnapshot(): current map of entity_id -> state for rendering.
 */
UCLASS()
class ARCANECLIENT_API UArcaneAdapterSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Manager base URL (e.g. http://127.0.0.1:8081). No trailing slash. */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane")
	FString ManagerUrl = TEXT("http://127.0.0.1:8081");

	/** Fired when connected to cluster and receiving state. */
	UPROPERTY(BlueprintAssignable, Category = "Arcane")
	FArcaneOnConnected OnConnected;

	/** Fired when disconnected. */
	UPROPERTY(BlueprintAssignable, Category = "Arcane")
	FArcaneOnDisconnected OnDisconnected;

	/** Fired when join or WebSocket connection fails. */
	UPROPERTY(BlueprintAssignable, Category = "Arcane")
	FArcaneOnConnectionFailed OnConnectionFailed;

	/** Initialize with manager URL. Call before Connect. */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void Initialize(const FString& InManagerUrl);

	/** Start connection: HTTP GET join, then WebSocket to cluster. Async; OnConnected or OnConnectionFailed when done. */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void Connect();

	/** Disconnect WebSocket and clear cache. */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void Disconnect();

	/** Call every frame from game thread. Drains inbound messages and updates entity cache. */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void Tick(float DeltaTime);

	/** Current entity snapshot (entity_id -> state). Read after Tick for rendering. */
	UFUNCTION(BlueprintCallable, Category = "Arcane", BlueprintPure)
	TArray<FArcaneEntityState> GetEntitySnapshot() const;

	/**
	 * Apply entity state to an actor using the engine's standard replicated-movement path.
	 * Use this for plug-and-play: your actor (e.g. same character class as the player) gets
	 * position/velocity applied the same way as a simulated proxy; movement, animation, and
	 * floor work with no custom character code.
	 * @param Actor Any actor (typically ACharacter); must support replicated movement (e.g. AActor).
	 *        For the apply path to run, set Actor->SetReplicatingMovement(true) on entity actors (e.g. in BeginPlay).
	 * @param State Entity state from GetEntitySnapshot() or GetInterpolatedEntitySnapshot().
	 * @param WorldOrigin Unreal world position of server origin (0,0,0). E.g. player location minus scale*server_center.
	 * @param PositionScale Scale from server units to Unreal units (1 = 1 cm per server unit).
	 */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void ApplyEntityStateToActor(AActor* Actor, const FArcaneEntityState& State, FVector WorldOrigin, float PositionScale) const;

	/**
	 * Interpolated snapshot for smooth movement. Use this for rendering so entities move continuously
	 * instead of snapping to server ticks. RenderTime = now - InterpolationDelay (e.g. 0.1s).
	 */
	UFUNCTION(BlueprintCallable, Category = "Arcane", BlueprintPure)
	TArray<FArcaneEntityState> GetInterpolatedEntitySnapshot(float InterpolationDelaySeconds) const;

	/** Send this client's player state to the cluster (position + velocity). Call each frame or at ~20 Hz. Uses same scale as server (divide by SendPositionScale). */
	UFUNCTION(BlueprintCallable, Category = "Arcane")
	void SendPlayerState(FVector Position, FVector Velocity);

	/** Unreal UU -> server units when sending. Use same value as Arcane Entity Display Position Scale so 1 server unit = 1 UU (1 cm). Default 1 = consistent with display scale 1. */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane", meta = (ClampMin = "0.01"))
	float SendPositionScale = 1.f;

	/** Enable automatic reconnect after connection failures. */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane|Connection")
	bool bEnableAutoReconnect = true;

	/** Max reconnect attempts after a failure (0 = disabled). */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane|Connection", meta = (ClampMin = "0"))
	int32 MaxReconnectAttempts = 3;

	/** Delay between reconnect attempts in seconds. */
	UPROPERTY(BlueprintReadWrite, Category = "Arcane|Connection", meta = (ClampMin = "0.1"))
	float ReconnectDelaySeconds = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Arcane", BlueprintPure)
	bool IsConnected() const { return bIsConnected; }

	UFUNCTION(BlueprintCallable, Category = "Arcane", BlueprintPure)
	EArcaneConnectionState GetConnectionState() const { return ConnectionState; }

	/** This client's entity ID (set when connecting). Used when sending PLAYER_STATE. */
	UPROPERTY(BlueprintReadOnly, Category = "Arcane")
	FString PlayerEntityId;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void StartJoinRequest();
	void HandleConnectionFailure(const FString& Reason);
	void BeginReconnect(const FString& Reason);
	void AttemptReconnectIfDue();
	void OnJoinResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
	void ConnectWebSocket(const FString& Host, int32 Port);
	void ParseStateUpdateJson(const FString& JsonString);
	void ApplyStateUpdate(const TArray<FArcaneEntityState>& Updated, const TArray<FString>& RemovedIds);

	bool bIsConnected = false;
	bool bManualDisconnect = false;
	EArcaneConnectionState ConnectionState = EArcaneConnectionState::Disconnected;
	int32 CurrentReconnectAttempt = 0;
	double NextReconnectAtSeconds = 0.0;
	TUniquePtr<FArcaneConnectionClient> ConnectionClient;
	TArray<FString> InboundMessageQueue;
	mutable FCriticalSection InboundQueueMutex;
	FArcaneEntityCache EntityCache;
};
