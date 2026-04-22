// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FChaosRigidAssetNodesModule : public IModuleInterface
{
public:

	CHAOSRIGIDASSETNODES_API virtual void StartupModule() override;
	CHAOSRIGIDASSETNODES_API virtual void ShutdownModule() override;

private:
};