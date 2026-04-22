// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "SceneOutlinerPublicTypes.h"
#include "Templates/SharedPointer.h"

#include "TedsOutlinerColumns.generated.h"

class ISceneOutliner;
class UToolMenu;
class SSceneOutliner;

namespace UE::Editor::Outliner
{
	// Delegate that refreshes columns on the Teds Outliner it is bound to when executed
	DECLARE_DELEGATE(FOnRefreshColumns)

	// Delegate called when the selection in the Outliner changes
	DECLARE_MULTICAST_DELEGATE(FOnTedsOutlinerSelectionChanged)

	// Delegate to get the TreeItemID for a row
	DECLARE_DELEGATE_RetVal_OneParam(FSceneOutlinerTreeItemID, FTreeItemIDDealiaser, DataStorage::RowHandle);

	// Mapping domain name for Outliners
	inline static const FName MappingDomain = "TedsOutliner";
}

// Column used to store a reference to the Teds Outliner owning a specific row
// Currently only added to widget rows in the Teds Outliner
USTRUCT(meta = (DisplayName = "Owning Table Viewer"))
struct FTedsOutlinerColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	TWeakPtr<ISceneOutliner> Outliner;
};

DECLARE_DELEGATE_TwoParams(FTedsOutlinerContextMenuDelegate, UToolMenu* Menu, SSceneOutliner& SceneOutliner)

/** Column used to allow context menu to be extended for an item in the outliner */
USTRUCT()
struct FTedsOutlinerContextMenuColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	FTedsOutlinerContextMenuDelegate OnCreateContextMenu;
};

/** Column used to store information about the columns being viewed in the Teds Outliner */
USTRUCT()
struct FTedsOutlinerColumnQueryColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	// The SELECT() part of the query description specifies the columns to be displayed
	UE::Editor::DataStorage::FQueryDescription ColumnQueryDescription;

	// Call this delegate to refresh the columns in the Teds Outliner with the values specified in ColumnQueryDescription
	UE::Editor::Outliner::FOnRefreshColumns OnRefreshColumns;
};

/** Column used to store a dealiaser from outliner item -> row handle */
USTRUCT()
struct FTedsOutlinerDealiaserColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UE::Editor::Outliner::FTreeItemIDDealiaser Dealiaser;
};

/** Column with a delegate that is broadcast when the outliner's selection changes */
USTRUCT()
struct FTedsOutlinerSelectionChangeColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UE::Editor::Outliner::FOnTedsOutlinerSelectionChanged OnSelectionChanged;
};