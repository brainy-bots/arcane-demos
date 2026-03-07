// Copyright Arcane Engine. Arcane Demo game module.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FArcaneDemoModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
