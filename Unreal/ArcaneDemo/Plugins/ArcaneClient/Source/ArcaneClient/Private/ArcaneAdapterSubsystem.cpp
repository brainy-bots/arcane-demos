// Copyright Arcane Engine. Adapter subsystem implementation.

#include "ArcaneAdapterSubsystem.h"
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
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

	FString JoinUrl = ManagerUrl + TEXT("/join");
	UE_LOG(LogArcaneAdapter, Log, TEXT("Connect: GET %s"), *JoinUrl);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(JoinUrl);
	Request->SetVerb(TEXT("GET"));
	Request->OnProcessRequestComplete().BindUObject(this, &UArcaneAdapterSubsystem::OnJoinResponseReceived);
	if (!Request->ProcessRequest())
	{
		OnConnectionFailed.Broadcast(TEXT("Failed to start HTTP request"));
	}
}

void UArcaneAdapterSubsystem::OnJoinResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		OnConnectionFailed.Broadcast(TEXT("HTTP request failed or no response"));
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		OnConnectionFailed.Broadcast(FString::Printf(TEXT("Join returned %d"), Response->GetResponseCode()));
		return;
	}

	FString JsonString = Response->GetContentAsString();
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OnConnectionFailed.Broadcast(TEXT("Invalid JSON from /join"));
		return;
	}

	FString ServerHost;
	int32 ServerPort = 8080;
	if (!JsonObject->TryGetStringField(TEXT("server_host"), ServerHost))
	{
		OnConnectionFailed.Broadcast(TEXT("Join response missing server_host"));
		return;
	}
	JsonObject->TryGetNumberField(TEXT("server_port"), ServerPort);

	UE_LOG(LogArcaneAdapter, Log, TEXT("Join OK: connecting WebSocket to %s:%d"), *ServerHost, ServerPort);
	ConnectWebSocket(ServerHost, ServerPort);
}

void UArcaneAdapterSubsystem::ConnectWebSocket(const FString& Host, int32 Port)
{
	FWebSocketsModule* const WsModule = FModuleManager::LoadModulePtr<FWebSocketsModule>(TEXT("WebSockets"));
	if (!WsModule)
	{
		OnConnectionFailed.Broadcast(TEXT("WebSockets module could not be loaded. Enable WebSocket support or check engine installation."));
		return;
	}

	FString WsUrl = FString::Printf(TEXT("ws://%s:%d"), *Host, Port);
	TSharedPtr<IWebSocket> Socket = WsModule->CreateWebSocket(WsUrl, TEXT("ws"));

	Socket->OnConnected().AddLambda([this]() {
		AsyncTask(ENamedThreads::GameThread, [this]() {
			if (PlayerEntityId.IsEmpty())
			{
				PlayerEntityId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			}
			bIsConnected = true;
			UE_LOG(LogArcaneAdapter, Log, TEXT("WebSocket connected (player_id=%s)"), *PlayerEntityId);
			OnConnected.Broadcast();
		});
	});
	Socket->OnConnectionError().AddLambda([this](const FString& Error) {
		AsyncTask(ENamedThreads::GameThread, [this, Error]() {
			bIsConnected = false;
			OnConnectionFailed.Broadcast(Error);
		});
	});
	Socket->OnMessage().AddLambda([this](const FString& Message) {
		FScopeLock Lock(&EntityCacheMutex);
		InboundMessageQueue.Add(Message);
	});
	Socket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) {
		AsyncTask(ENamedThreads::GameThread, [this, Reason]() {
			bIsConnected = false;
			OnDisconnected.Broadcast(Reason);
		});
	});

	WebSocket = Socket;
	WebSocket->Connect();
}

void UArcaneAdapterSubsystem::Disconnect()
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		WebSocket->Close();
	}
	WebSocket.Reset();
	bIsConnected = false;
	{
		FScopeLock Lock(&EntityCacheMutex);
		InboundMessageQueue.Empty();
		EntityCache.Empty();
		PreviousEntityCache.Empty();
		CurrentSnapshotTime = 0.0;
		PreviousSnapshotTime = 0.0;
	}
}

void UArcaneAdapterSubsystem::Tick(float DeltaTime)
{
	TArray<FString> ToProcess;
	{
		FScopeLock Lock(&EntityCacheMutex);
		ToProcess = MoveTemp(InboundMessageQueue);
		InboundMessageQueue.Empty();
	}
	for (const FString& Message : ToProcess)
	{
		ParseStateUpdateJson(Message);
	}
}

void UArcaneAdapterSubsystem::ParseStateUpdateJson(const FString& JsonString)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogArcaneAdapter, Warning, TEXT("ParseStateUpdateJson: invalid JSON (first 200 chars): %s"), *JsonString.Left(200));
		return;
	}

	TArray<FArcaneEntityState> Updated;
	const TArray<TSharedPtr<FJsonValue>>* UpdatedArray;
	if (JsonObject->TryGetArrayField(TEXT("updated"), UpdatedArray))
	{
		for (const TSharedPtr<FJsonValue>& EntryVal : *UpdatedArray)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!EntryVal->TryGetObject(EntryObj) || !EntryObj->IsValid()) continue;

			FString EntityId;
			if (!(*EntryObj)->TryGetStringField(TEXT("entity_id"), EntityId)) continue;

			FString ClusterId;
			(*EntryObj)->TryGetStringField(TEXT("cluster_id"), ClusterId);

			FVector Position = FVector::ZeroVector;
			const TSharedPtr<FJsonObject>* PosObj;
			if ((*EntryObj)->TryGetObjectField(TEXT("position"), PosObj) && PosObj->IsValid())
			{
				(*PosObj)->TryGetNumberField(TEXT("x"), Position.X);
				(*PosObj)->TryGetNumberField(TEXT("y"), Position.Y);
				(*PosObj)->TryGetNumberField(TEXT("z"), Position.Z);
			}

			FVector Velocity = FVector::ZeroVector;
			const TSharedPtr<FJsonObject>* VelObj;
			if ((*EntryObj)->TryGetObjectField(TEXT("velocity"), VelObj) && VelObj->IsValid())
			{
				(*VelObj)->TryGetNumberField(TEXT("x"), Velocity.X);
				(*VelObj)->TryGetNumberField(TEXT("y"), Velocity.Y);
				(*VelObj)->TryGetNumberField(TEXT("z"), Velocity.Z);
			}

			Updated.Add(FArcaneEntityState(EntityId, ClusterId, Position, Velocity));
		}
	}

	TArray<FString> RemovedIds;
	const TArray<TSharedPtr<FJsonValue>>* RemovedArray;
	if (JsonObject->TryGetArrayField(TEXT("removed"), RemovedArray))
	{
		for (const TSharedPtr<FJsonValue>& IdVal : *RemovedArray)
		{
			FString Id;
			if (IdVal->TryGetString(Id))
			{
				RemovedIds.Add(Id);
			}
		}
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
	FScopeLock Lock(&EntityCacheMutex);
	// Keep previous snapshot for interpolation (smooth movement between server ticks)
	PreviousEntityCache = EntityCache;
	PreviousSnapshotTime = CurrentSnapshotTime;
	CurrentSnapshotTime = FPlatformTime::Seconds();

	// Server sends full snapshot every tick; replace cache with it so client stays in sync (no stale positions).
	if (Updated.Num() > 0)
	{
		EntityCache.Empty();
		for (const FArcaneEntityState& State : Updated)
		{
			EntityCache.Add(State.EntityId, State);
		}
	}
	for (const FString& Id : RemovedIds)
	{
		EntityCache.Remove(Id);
	}
}

TArray<FArcaneEntityState> UArcaneAdapterSubsystem::GetEntitySnapshot() const
{
	FScopeLock Lock(&EntityCacheMutex);
	TArray<FArcaneEntityState> Out;
	for (const auto& Pair : EntityCache)
	{
		Out.Add(Pair.Value);
	}
	return Out;
}

TArray<FArcaneEntityState> UArcaneAdapterSubsystem::GetInterpolatedEntitySnapshot(float InterpolationDelaySeconds) const
{
	FScopeLock Lock(&EntityCacheMutex);
	TArray<FArcaneEntityState> Out;
	const double Now = FPlatformTime::Seconds();
	const double RenderTime = Now - static_cast<double>(InterpolationDelaySeconds);

	for (const auto& Pair : EntityCache)
	{
		const FArcaneEntityState& Current = Pair.Value;
		const FArcaneEntityState* PrevState = PreviousEntityCache.Find(Pair.Key);

		if (!PrevState || PreviousSnapshotTime >= CurrentSnapshotTime)
		{
			// No previous or same time: use current (or extrapolate forward)
			if (RenderTime >= CurrentSnapshotTime && CurrentSnapshotTime > 0.0)
			{
				const double Dt = RenderTime - CurrentSnapshotTime;
				FArcaneEntityState Extrapolated = Current;
				Extrapolated.Position += Current.Velocity * Dt;
				Out.Add(Extrapolated);
			}
			else
			{
				Out.Add(Current);
			}
			continue;
		}

		const double T0 = PreviousSnapshotTime;
		const double T1 = CurrentSnapshotTime;
		if (RenderTime <= T0)
		{
			Out.Add(*PrevState);
			continue;
		}
		if (RenderTime >= T1)
		{
			Out.Add(Current);
			continue;
		}

		const double T = (RenderTime - T0) / (T1 - T0);
		FArcaneEntityState Interp;
		Interp.EntityId = Current.EntityId;
		Interp.ClusterId = Current.ClusterId;
		Interp.Position = FMath::Lerp(PrevState->Position, Current.Position, static_cast<float>(T));
		Interp.Velocity = FMath::Lerp(PrevState->Velocity, Current.Velocity, static_cast<float>(T));
		Out.Add(Interp);
	}
	return Out;
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
	if (!WebSocket.IsValid() || !WebSocket->IsConnected() || PlayerEntityId.IsEmpty())
	{
		return;
	}
	const float Scale = SendPositionScale > 0.f ? SendPositionScale : 1.f;
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"PLAYER_STATE\",\"entity_id\":\"%s\",\"position\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f},\"velocity\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f}}"),
		*PlayerEntityId,
		Position.X / Scale, Position.Z / Scale, Position.Y / Scale,
		Velocity.X / Scale, Velocity.Z / Scale, Velocity.Y / Scale
	);
	WebSocket->Send(Json);
}
