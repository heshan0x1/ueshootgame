// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"

#define UE_API INSTANCEDATAOBJECTFIXUPTOOL_API

class FInstanceDataObjectFixupToolModule : public FDefaultModuleImpl
{
public:
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;
	UE_API bool OpenInstanceDataObjectFixupTool() const;
	static FInstanceDataObjectFixupToolModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FInstanceDataObjectFixupToolModule>("InstanceDataObjectFixupTool");
	}

	// opens the fixup tool 
	UE_API TSharedRef<SDockTab> CreateInstanceDataObjectFixupTab(
		const FSpawnTabArgs& TabArgs, 
		TConstArrayView<TObjectPtr<UObject>> InstanceDataObjects, 
		TObjectPtr<UObject> InstanceDataObjectsOwner = nullptr) const;
	
	UE_API void CreateInstanceDataObjectFixupDialog(
		TConstArrayView<TObjectPtr<UObject>> InstanceDataObjects, 
		TObjectPtr<UObject> InstanceDataObjectsOwner = nullptr) const;
};

#undef UE_API
