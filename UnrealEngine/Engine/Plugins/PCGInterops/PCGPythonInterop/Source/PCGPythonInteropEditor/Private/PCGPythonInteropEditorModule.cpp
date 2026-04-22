// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPythonInteropEditorModule.h"
#include "Modules/ModuleManager.h"

class FPCGPythonInteropEditorModule final : public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
	//~ End IModuleInterface implementation
};

IMPLEMENT_MODULE(FPCGPythonInteropEditorModule, PCGPythonInteropEditor);

DEFINE_LOG_CATEGORY(LogPCGPython);