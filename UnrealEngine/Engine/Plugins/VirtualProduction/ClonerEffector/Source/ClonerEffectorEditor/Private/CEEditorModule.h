// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FCEEditorInputPreprocessor;
class FCEEditorThrottleManager;

class FCEEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
	//~ End IModuleInterface

	static TSharedPtr<FCEEditorInputPreprocessor> GetInputPreprocessor();

protected:
	FDelegateHandle ClonerTrackCreateEditorHandle;
	TSharedPtr<FCEEditorThrottleManager> ThrottleManager;
	TSharedPtr<FCEEditorInputPreprocessor> InputPreprocessor;
	TArray<FName> CustomizationNames;
};
