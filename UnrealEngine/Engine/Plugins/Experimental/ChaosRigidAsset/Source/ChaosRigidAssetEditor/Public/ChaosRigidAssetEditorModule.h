// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "IPhysicsAssetEditor.h"
#include "Delegates/IDelegateInstance.h"

class FDataflowPhysicsAssetEditorOverride;

class FChaosRigidAssetEditorModule : public IModuleInterface
{
public:

	CHAOSRIGIDASSETEDITOR_API virtual void StartupModule() override;
	CHAOSRIGIDASSETEDITOR_API virtual void ShutdownModule() override;

private:
	TUniquePtr<FDataflowPhysicsAssetEditorOverride> EditorFeature;
	FDelegateHandle AssetMenuExtenderHandle;
};
