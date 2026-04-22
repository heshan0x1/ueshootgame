// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleManager.h"

class FEaseCurveToolModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	void RegisterContentBrowserExtender();
	void UnregisterContentBrowserExtender();

	void RegisterCurveEditorExtender();
	void UnregisterCurveEditorExtender();

	FDelegateHandle ContentBrowserExtenderHandle;
	FDelegateHandle CurveEditorExtenderHandle;
};
