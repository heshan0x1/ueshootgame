// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerMode.h"
#include "TedsOutlinerImpl.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"

#include "TedsOutlinerMode.generated.h"

#define UE_API TEDSOUTLINER_API

namespace UE::Editor::Outliner
{
// TEDS-Outliner TODO: This can probably be moved to a more generic location for all TEDS related drag drops?
class FTedsRowDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FTedsRowDragDropOp, FDecoratedDragDropOp)

	/** Rows we are dragging */
	TArray<DataStorage::RowHandle> DraggedRows;

	void Init(const TArray<DataStorage::RowHandle>& InRowHandles)
	{
		DraggedRows = InRowHandles;
	}

	static TSharedRef<FTedsRowDragDropOp> New(const TArray<DataStorage::RowHandle>& InRowHandles)
	{
		TSharedRef<FTedsRowDragDropOp> Operation = MakeShareable(new FTedsRowDragDropOp);
		
		Operation->Init(InRowHandles);
		Operation->SetupDefaults();
		Operation->Construct();
		
		return Operation;
	}
};

/*
 * TEDS driven Outliner mode where the Outliner is populated using the results of the RowHandleQueries passed in.
 * See CreateGenericTEDSOutliner() for example usage
 * Inherits from ISceneOutlinerMode - which contains all actions that depend on the type of item you are viewing in the Outliner
 */
class FTedsOutlinerMode : public ISceneOutlinerMode
{
public:
	UE_API explicit FTedsOutlinerMode(const FTedsOutlinerParams& InParams);
	UE_API virtual ~FTedsOutlinerMode() override;

	/* ISceneOutlinerMode interface */
	UE_API virtual void Rebuild() override;
	UE_API virtual void SynchronizeSelection() override;
	UE_API virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }
	virtual bool CanSupportDragAndDrop() const override { return true; } // TODO: Can we check this from TEDS somehow (if the user requests a drag column?)
	UE_API virtual TSharedPtr<FDragDropOperation> CreateDragDropOperation(const FPointerEvent& MouseEvent, const TArray<FSceneOutlinerTreeItemPtr>& InTreeItems) const override;
	UE_API virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override;
	UE_API virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override;
	UE_API virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override;
	UE_API virtual TSharedPtr<SWidget> CreateContextMenu() override;
	UE_API virtual void Tick() override;
	/* end ISceneOutlinerMode interface */

protected:
	
	UE_API virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;
	// Called by TedsOutlinerImpl when the selection in TEDS changes
	UE_API void OnSelectionChanged();

protected:

	// The actual model for the TEDS Outliner
	TSharedPtr<FTedsOutlinerImpl> TedsOutlinerImpl;
};
} // namespace UE::Editor::Outliner

// Class to hold the owning scene outliner for a menu
// TEDS-Outliner TODO: Once menus go through TEDS UI this can be done using the FTableViewerColumn on the widget row instead
UCLASS()
class UTedsOutlinerMenuContext : public UObject
{
	GENERATED_BODY()
public:
	SSceneOutliner* OwningSceneOutliner = nullptr;
};

#undef UE_API
