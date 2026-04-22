// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"
#include "Kismet2/StructureEditorUtils.h"

#define UE_API STRUCTUTILSEDITOR_API

class IStructUtilsEditor;
struct FGraphPanelNodeFactory;
class UUserDefinedStruct;

/**
* The public interface to this module
*/
class FStructUtilsEditorModule : public IModuleInterface, public FStructureEditorUtils::INotifyOnStructChanged
{
public:
	// Begin IModuleInterface
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;

protected:

	// INotifyOnStructChanged
	UE_API virtual void PreChange(const class UUserDefinedStruct* Struct, FStructureEditorUtils::EStructureEditorChangeInfo Info) override;
	UE_API virtual void PostChange(const class UUserDefinedStruct* Struct, FStructureEditorUtils::EStructureEditorChangeInfo Info) override;
	// ~INotifyOnStructChanged
};

#undef UE_API
