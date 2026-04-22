// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Modules/ModuleInterface.h"
#include "UObject/NameTypes.h"

namespace UE::SceneStateDataLink
{

class FEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	TArray<FName> CustomizedTypes;
};

} // UE::SceneStateDataLink