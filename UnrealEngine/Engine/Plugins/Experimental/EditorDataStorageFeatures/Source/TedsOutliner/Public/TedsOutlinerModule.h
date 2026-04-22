// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/Handles.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "Delegates/IDelegateInstance.h"
#include "DataStorage/Queries/Description.h"

class ISceneOutliner;
struct FSceneOutlinerInitializationOptions;
class SDockTab;
class SWidget;
class FSpawnTabArgs;

namespace UE::Editor::Outliner
{
	struct FTedsOutlinerParams;

/**
 * Implements the Scene Outliner module.
 */
class FTedsOutlinerModule
	: public IModuleInterface
{
public:

	FTedsOutlinerModule();

	/**
	 * Creates a TEDS-Outliner widget
	 *
	 * @param	InitOptions						Programmer-driven configuration for the scene outliner
	 * @param	InInitTedsOptions				Programmer-driven configuration for the TEDS queries that drive the outliner
	 * @param   ColumnQuery						Query to describe which columns will be available in the TEDS-Outliner
	 *
	 * @return	New scene outliner widget
	 */
	virtual TSharedRef<ISceneOutliner> CreateTedsOutliner(
		const FSceneOutlinerInitializationOptions& InInitOptions, const FTedsOutlinerParams& InInitTedsOptions) const;

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Get the column query the default table viewer uses
	virtual UE::Editor::DataStorage::FQueryDescription GetLevelEditorTedsOutlinerColumnQueryDescription();

	// The name of the tab the default table viewer is opened in
	FName GetTedsOutlinerTabName();

private:
	
	void RegisterLevelEditorTedsOutlinerTab();
	void UnregisterLevelEditorTedsOutlinerTab();
	TSharedRef<SDockTab> OpenLevelEditorTedsOutliner(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SWidget> CreateLevelEditorTedsOutliner();
	
private:
	FName TedsOutlinerTabName;
	FDelegateHandle LevelEditorTabManagerChangedHandle;
	DataStorage::ICoreProvider* Storage = nullptr;
};
} // namespace UE::Editor::Outliner
