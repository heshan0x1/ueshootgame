// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/Handles.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "SceneOutlinerFwd.h"
#include "Columns/TedsOutlinerColumns.h"

// Helper functions for Teds Outliner related functionality
namespace UE::Editor::Outliner::Helpers
{
	// Get the internal Outliner tree item from the row handle from the specific Outliner
	TEDSOUTLINER_API FSceneOutlinerTreeItemPtr GetTreeItemFromRowHandle(const DataStorage::ICoreProvider* Storage, TSharedRef<ISceneOutliner> InOutliner, DataStorage::RowHandle InRowHandle);

	// Register a dealiaser for the given outliner, overriding any previously registered ones.
	// Returns false if the registration failed, mostly commonly because the Outliner doesn't have a unique OutlinerIdentifier specificed
	TEDSOUTLINER_API bool RegisterOutlinerDealiaser(DataStorage::ICoreProvider* Storage, TSharedRef<ISceneOutliner> InOutliner, const FTreeItemIDDealiaser& InDealiaser);

	// Get the name of the Teds table that all Outliners are stored in
	TEDSOUTLINER_API FName GetTedsOutlinerTableName();
	
	// Refresh all the Outliners currently open in the level editor
	TEDSOUTLINER_API void RefreshLevelEditorOutliners();
	
	// Get the name of the Outliner column corresponding to the given TEDS column (if any)
	FName FindOutlinerColumnFromTedsColumns(const DataStorage::ICoreProvider* Storage, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> TEDSColumns);
	
	// Helper function to check if the given QueryHandle is valid to be used in a filter (is not an observer).
    bool CheckValidFilterQueryHandle(const DataStorage::QueryHandle& InQueryHandle);
}
