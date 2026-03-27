#pragma once

#include "CoreMinimal.h"

class IWebSocket;

struct FArcaneConnectionClientCallbacks
{
	TFunction<void()> OnConnected;
	TFunction<void(const FString&)> OnConnectionError;
	TFunction<void(const FString&)> OnMessage;
	TFunction<void(int32, const FString&, bool)> OnClosed;
};

class FArcaneConnectionClient
{
public:
	bool Connect(const FString& Host, int32 Port, FArcaneConnectionClientCallbacks&& InCallbacks, FString& OutError);
	void Disconnect();
	bool IsConnected() const;
	void Send(const FString& Text) const;

private:
	TSharedPtr<IWebSocket> Socket;
	FArcaneConnectionClientCallbacks Callbacks;
};
