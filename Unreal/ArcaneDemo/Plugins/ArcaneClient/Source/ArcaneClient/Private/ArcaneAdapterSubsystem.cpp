// Copyright Arcane Engine. Adapter subsystem implementation.

#include "ArcaneAdapterSubsystem.h"
#include "ArcaneConnectionClient.h"
#include "ArcaneProtocolCodec.h"
#include "ArcaneTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/ReplicatedState.h"
#include "HttpModule.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneAdapter, Log, All);
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformTime.h"

void UArcaneAdapterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UArcaneAdapterSubsystem::Deinitialize()
{
	Disconnect();
	Super::Deinitialize();
}

void UArcaneAdapterSubsystem::Initialize(const FString& InManagerUrl)
{
	ManagerUrl = InManagerUrl;
	if (ManagerUrl.EndsWith(TEXT("/")))
	{
		ManagerUrl.LeftChopInline(1);
	}
}

void UArcaneAdapterSubsystem::Connect()
{
	Disconnect();
	bManualDisconnect = false;
	CurrentReconnectAttempt = 0;
	NextReconnectAtSeconds = 0.0;
	ConnectionState = EArcaneConnectionState::Joining;
	StartJoinRequest();
}

void UArcaneAdapterSubsystem::StartJoinRequest()
{
	FString JoinUrl = ManagerUrl + TEXT("/join");
	UE_LOG(LogArcaneAdapter, Log, TEXT("Connect: GET %s"), *JoinUrl);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(JoinUrl);
	Request->SetVerb(TEXT("GET"));
	Request->OnProcessRequestComplete().BindUObject(this, &UArcaneAdapterSubsystem::OnJoinResponseReceived);
	if (!Request->ProcessRequest())
	{
		HandleConnectionFailure(TEXT("Failed to start HTTP request"));
	}
}

void UArcaneAdapterSubsystem::OnJoinResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		HandleConnectionFailure(TEXT("HTTP request failed or no response"));
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		HandleConnectionFailure(FString::Printf(TEXT("Join returned %d"), Response->GetResponseCode()));
		return;
	}

	FString ServerHost;
	int32 ServerPort = 8080;
	FString ParseError;
	if (!ArcaneProtocolCodec::ParseJoinResponse(Response->GetContentAsString(), ServerHost, ServerPort, ParseError))
	{
		HandleConnectionFailure(ParseError);
		return;
	}

	ConnectionState = EArcaneConnectionState::ConnectingWebSocket;
	UE_LOG(LogArcaneAdapter, Log, TEXT("Join OK: connecting WebSocket to %s:%d"), *ServerHost, ServerPort);
	ConnectWebSocket(ServerHost, ServerPort);
}

void UArcaneAdapterSubsystem::ConnectWebSocket(const FString& Host, int32 Port)
{
	ConnectionClient = MakeUnique<FArcaneConnectionClient>();
	FString ConnectError;
	const bool bStarted = ConnectionClient->Connect(
		Host,
		Port,
		FArcaneConnectionClientCallbacks{
			[this]() {
				AsyncTask(ENamedThreads::GameThread, [this]() {
					if (PlayerEntityId.IsEmpty())
					{
						PlayerEntityId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
					}
					bIsConnected = true;
					ConnectionState = EArcaneConnectionState::Connected;
					CurrentReconnectAttempt = 0;
					UE_LOG(LogArcaneAdapter, Log, TEXT("WebSocket connected (player_id=%s)"), *PlayerEntityId);
					OnConnected.Broadcast();
				});
			},
			[this](const FString& Error) {
				AsyncTask(ENamedThreads::GameThread, [this, Error]() {
					bIsConnected = false;
					HandleConnectionFailure(Error);
				});
			},
			[this](const FString& Message) {
				FScopeLock Lock(&InboundQueueMutex);
				InboundMessageQueue.Add(Message);
			},
			[this](int32 StatusCode, const FString& Reason, bool bWasClean) {
				AsyncTask(ENamedThreads::GameThread, [this, Reason]() {
					bIsConnected = false;
					OnDisconnected.Broadcast(Reason);
					if (!bManualDisconnect)
					{
						HandleConnectionFailure(FString::Printf(TEXT("WebSocket closed: %s"), *Reason));
					}
				});
			}
		},
		ConnectError
	);
	if (!bStarted)
	{
		HandleConnectionFailure(ConnectError);
		return;
	}
}

void UArcaneAdapterSubsystem::Disconnect()
{
	bManualDisconnect = true;
	ConnectionState = EArcaneConnectionState::Disconnected;
	NextReconnectAtSeconds = 0.0;
	if (ConnectionClient.IsValid())
	{
		ConnectionClient->Disconnect();
		ConnectionClient.Reset();
	}
	bIsConnected = false;
	{
		FScopeLock Lock(&InboundQueueMutex);
		InboundMessageQueue.Empty();
	}
	EntityCache.Reset();
}

void UArcaneAdapterSubsystem::Tick(float DeltaTime)
{
	AttemptReconnectIfDue();

	TArray<FString> ToProcess;
	{
		FScopeLock Lock(&InboundQueueMutex);
		ToProcess = MoveTemp(InboundMessageQueue);
		InboundMessageQueue.Empty();
	}
	for (const FString& Message : ToProcess)
	{
		ParseStateUpdateJson(Message);
	}
}

void UArcaneAdapterSubsystem::HandleConnectionFailure(const FString& Reason)
{
	if (ConnectionClient.IsValid())
	{
		ConnectionClient->Disconnect();
		ConnectionClient.Reset();
	}
	bIsConnected = false;

	if (!bEnableAutoReconnect || bManualDisconnect || MaxReconnectAttempts <= 0)
	{
		ConnectionState = EArcaneConnectionState::Failed;
		OnConnectionFailed.Broadcast(Reason);
		return;
	}

	if (CurrentReconnectAttempt >= MaxReconnectAttempts)
	{
		ConnectionState = EArcaneConnectionState::Failed;
		OnConnectionFailed.Broadcast(FString::Printf(TEXT("%s (retries exhausted)"), *Reason));
		return;
	}

	BeginReconnect(Reason);
}

void UArcaneAdapterSubsystem::BeginReconnect(const FString& Reason)
{
	CurrentReconnectAttempt++;
	ConnectionState = EArcaneConnectionState::Reconnecting;
	NextReconnectAtSeconds = FPlatformTime::Seconds() + static_cast<double>(ReconnectDelaySeconds);
	OnConnectionFailed.Broadcast(
		FString::Printf(
			TEXT("%s (retry %d/%d in %.1fs)"),
			*Reason,
			CurrentReconnectAttempt,
			MaxReconnectAttempts,
			ReconnectDelaySeconds
		)
	);
}

void UArcaneAdapterSubsystem::AttemptReconnectIfDue()
{
	if (ConnectionState != EArcaneConnectionState::Reconnecting || bManualDisconnect)
	{
		return;
	}
	if (FPlatformTime::Seconds() < NextReconnectAtSeconds)
	{
		return;
	}
	ConnectionState = EArcaneConnectionState::Joining;
	StartJoinRequest();
}

void UArcaneAdapterSubsystem::ParseStateUpdateJson(const FString& JsonString)
{
	TArray<FArcaneEntityState> Updated;
	TArray<FString> RemovedIds;
	if (!ArcaneProtocolCodec::ParseStateUpdate(JsonString, Updated, RemovedIds))
	{
		UE_LOG(LogArcaneAdapter, Warning, TEXT("ParseStateUpdateJson: invalid JSON (first 200 chars): %s"), *JsonString.Left(200));
		return;
	}

	ApplyStateUpdate(Updated, RemovedIds);
}

void UArcaneAdapterSubsystem::ApplyStateUpdate(const TArray<FArcaneEntityState>& Updated, const TArray<FString>& RemovedIds)
{
	static int32 TotalStateUpdatesApplied = 0;
	if (Updated.Num() > 0) { TotalStateUpdatesApplied++; }
	// Throttle: log at most once per second to avoid spamming (demo runs at ~20 Hz)
	static float LastStateLogTime = 0.f;
	const float Now = static_cast<float>(FPlatformTime::Seconds());
	if ((Updated.Num() > 0 || RemovedIds.Num() > 0) && (Now - LastStateLogTime >= 1.f))
	{
		UE_LOG(LogArcaneAdapter, Log, TEXT("State update: +%d entities, -%d removed (total updates applied: %d)"), Updated.Num(), RemovedIds.Num(), TotalStateUpdatesApplied);
		LastStateLogTime = Now;
	}
	EntityCache.ApplyStateUpdate(Updated, RemovedIds);
}

TArray<FArcaneEntityState> UArcaneAdapterSubsystem::GetEntitySnapshot() const
{
	return EntityCache.GetSnapshot();
}

TArray<FArcaneEntityState> UArcaneAdapterSubsystem::GetInterpolatedEntitySnapshot(float InterpolationDelaySeconds) const
{
	return EntityCache.GetInterpolatedSnapshot(InterpolationDelaySeconds);
}

void UArcaneAdapterSubsystem::ApplyEntityStateToActor(AActor* Actor, const FArcaneEntityState& State, FVector WorldOrigin, float PositionScale) const
{
	if (!Actor || PositionScale <= 0.f) return;

	const FVector WorldPos = WorldOrigin + PositionScale * State.Position;
	const FVector WorldVel = PositionScale * State.Velocity;
	FRotator Rot = FRotator::ZeroRotator;
	if (WorldVel.SizeSquared() > 1e-4f)
	{
		Rot = WorldVel.Rotation();
		Rot.Pitch = 0.f;
		Rot.Roll = 0.f;
	}

	FRepMovement RepMov;
	RepMov.Location = WorldPos;
	RepMov.Rotation = Rot;
	RepMov.LinearVelocity = WorldVel;
	RepMov.AngularVelocity = FVector::ZeroVector;
	RepMov.bRepPhysics = false;
	RepMov.bSimulatedPhysicSleep = false;

	Actor->SetReplicatedMovement(RepMov);
	// OnRep_ReplicatedMovement only applies position/velocity when GetLocalRole() == ROLE_SimulatedProxy.
	// In standalone (or authority) entity actors are not simulated proxies, so we apply directly.
	if (Actor->GetLocalRole() == ROLE_SimulatedProxy)
	{
		Actor->OnRep_ReplicatedMovement();
	}
	else
	{
		Actor->SetActorLocationAndRotation(WorldPos, Rot, /*bSweep=*/false);
		if (ACharacter* Char = Cast<ACharacter>(Actor))
		{
			if (UCharacterMovementComponent* MC = Char->GetCharacterMovement())
			{
				MC->Velocity = WorldVel;
			}
		}
	}
}

void UArcaneAdapterSubsystem::SendPlayerState(FVector Position, FVector Velocity)
{
	if (!ConnectionClient.IsValid() || !ConnectionClient->IsConnected() || PlayerEntityId.IsEmpty())
	{
		return;
	}
	const FString Json = ArcaneProtocolCodec::BuildPlayerStateJson(PlayerEntityId, Position, Velocity, SendPositionScale);
	ConnectionClient->Send(Json);
}
