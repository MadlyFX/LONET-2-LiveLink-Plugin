///COPYRIGHT 2021 (C) LOLED VIRTUAL LLC

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLONET2LiveLinkModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
