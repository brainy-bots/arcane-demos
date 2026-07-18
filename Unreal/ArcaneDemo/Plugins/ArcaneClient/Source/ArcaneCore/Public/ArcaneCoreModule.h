// Copyright Arcane Engine. Arcane Core module initialization.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FArcaneCore : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
