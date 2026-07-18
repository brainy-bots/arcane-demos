// Copyright Arcane Engine. Server module implementation for cluster node integration.

#include "ArcaneServerModule.h"
#include "Modules/ModuleManager.h"

class FArcaneServerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Warning, TEXT("ArcaneServer module loaded."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogTemp, Warning, TEXT("ArcaneServer module unloaded."));
	}
};

IMPLEMENT_MODULE(FArcaneServerModule, ArcaneServer);
