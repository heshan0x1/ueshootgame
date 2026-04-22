// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"

#define UE_API MASSGAMEPLAYEDITOR_API

/**
* The public interface to this module
*/
class FMassGameplayEditorModule : public IModuleInterface
{
public:
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;

protected:
	UE_API void RegisterSectionMappings();
};

#undef UE_API
