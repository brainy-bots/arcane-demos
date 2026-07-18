#include "ArcaneConnectionClient.h"

#include "IWebSocket.h"
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"

bool FArcaneConnectionClient::Connect(
	const FString& Host,
	int32 Port,
	FArcaneConnectionClientCallbacks&& InCallbacks,
	FString& OutError
)
{
	Callbacks = MoveTemp(InCallbacks);

	FWebSocketsModule* const WsModule = FModuleManager::LoadModulePtr<FWebSocketsModule>(TEXT("WebSockets"));
	if (!WsModule)
	{
		OutError = TEXT("WebSockets module could not be loaded. Enable WebSocket support or check engine installation.");
		return false;
	}

	const FString WsUrl = FString::Printf(TEXT("ws://%s:%d"), *Host, Port);
	Socket = WsModule->CreateWebSocket(WsUrl, TEXT("ws"));
	if (!Socket.IsValid())
	{
		OutError = TEXT("Failed to create WebSocket client");
		return false;
	}

	Socket->OnConnected().AddLambda([this]() {
		if (Callbacks.OnConnected)
		{
			Callbacks.OnConnected();
		}
	});
	Socket->OnConnectionError().AddLambda([this](const FString& Error) {
		if (Callbacks.OnConnectionError)
		{
			Callbacks.OnConnectionError(Error);
		}
	});
	Socket->OnRawMessage().AddLambda([this](const void* Data, SIZE_T Size, SIZE_T BytesRemaining) {
		if (Callbacks.OnMessage)
		{
			TArray<uint8> Bytes((const uint8*)Data, Size);
			Callbacks.OnMessage(Bytes);
		}
	});
	Socket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) {
		if (Callbacks.OnClosed)
		{
			Callbacks.OnClosed(StatusCode, Reason, bWasClean);
		}
	});

	Socket->Connect();
	return true;
}

void FArcaneConnectionClient::Disconnect()
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Close();
	}
	Socket.Reset();
}

bool FArcaneConnectionClient::IsConnected() const
{
	return Socket.IsValid() && Socket->IsConnected();
}

void FArcaneConnectionClient::Send(const TArray<uint8>& Data) const
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Send(Data.GetData(), Data.Num(), /*bIsBinary=*/true);
	}
}
