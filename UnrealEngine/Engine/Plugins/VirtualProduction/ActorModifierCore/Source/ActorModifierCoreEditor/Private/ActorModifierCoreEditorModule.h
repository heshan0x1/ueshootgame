// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Containers/Array.h"
#include "UObject/NameTypes.h"

class FActorModifierCoreEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

protected:
	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	void RegisterBlueprintCustomizations();

	TArray<FName> CustomizationNames;
};