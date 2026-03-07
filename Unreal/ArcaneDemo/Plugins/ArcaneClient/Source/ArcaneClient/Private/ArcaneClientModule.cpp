// Copyright Arcane Engine. Explicit module implementation so plugin loads without requiring WebSockets at startup.

#include "Modules/ModuleManager.h"

class FArcaneClientModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Dependencies (HTTP, Json, WebSockets) are loaded by the engine before we run.
		// We do not load WebSockets here so that if it fails elsewhere, the engine still reports the real cause.
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FArcaneClientModule, ArcaneClient);
