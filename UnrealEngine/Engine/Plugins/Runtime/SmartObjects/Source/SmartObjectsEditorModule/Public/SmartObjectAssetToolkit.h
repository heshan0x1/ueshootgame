// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AdvancedPreviewScene.h" // IWYU pragma: keep
#include "Tools/BaseAssetToolkit.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "EditorUndoClient.h"
#include "Widgets/Views/STreeView.h"

#define UE_API SMARTOBJECTSEDITORMODULE_API

class FAdvancedPreviewScene;
class FSpawnTabArgs;
class FEditorViewportClient;
class UAssetEditor;
class IStructureDetailsView;
class FSmartObjectViewModel;
class USmartObjectDetailsWrapper;
class USmartObjectDefinition;
struct FSmartObjectDefinitionPreviewData;
enum class EItemDropZone;
struct FSmartObjectOutlinerItem;

class FSmartObjectAssetToolkit : public FBaseAssetToolkit, public FSelfRegisteringEditorUndoClient, public FGCObject
{
public:
	UE_API explicit FSmartObjectAssetToolkit(UAssetEditor* InOwningAssetEditor);
	UE_API virtual ~FSmartObjectAssetToolkit();

	UE_API virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;

protected:
	UE_API virtual void PostInitAssetEditor() override;
	UE_API virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	UE_API virtual void OnClose() override;
	UE_API virtual void SetEditingObject(class UObject* InObject) override;
	
	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FSmartObjectAssetToolkit");
	}

	UE_API virtual void PostUndo(bool bSuccess) override;
	UE_API virtual void PostRedo(bool bSuccess) override;
	
private:

	UE_API void OnParametersChanged(const USmartObjectDefinition& SmartObjectDefinition);

	UE_API void UpdatePreviewActor();
	UE_API void UpdateCachedPreviewDataFromDefinition();

	UE_API void UpdateItemList();
	
	/** Callback to detect changes in number of slot to keep gizmos in sync. */
	UE_API void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

	/** Creates a tab allowing the user to select a mesh or actor template to spawn in the preview scene. */
	UE_API TSharedRef<SDockTab> SpawnTab_PreviewSettings(const FSpawnTabArgs& Args);
	UE_API TSharedRef<SDockTab> SpawnTab_Outliner(const FSpawnTabArgs& Args);
	UE_API TSharedRef<SDockTab> SpawnTab_SelectionDetails(const FSpawnTabArgs& Args);
	UE_API TSharedRef<SDockTab> SpawnTab_SceneViewport(const FSpawnTabArgs& Args);

	UE_API TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FSmartObjectOutlinerItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	UE_API void OnGetChildren(TSharedPtr<FSmartObjectOutlinerItem> InItem, TArray<TSharedPtr<FSmartObjectOutlinerItem>>& OutChildren) const;
	UE_API void OnOutlinerSelectionChanged(TSharedPtr<FSmartObjectOutlinerItem> SelectedItem, ESelectInfo::Type SelectType);
	UE_API TSharedPtr<SWidget> OnOutlinerContextMenu();
	UE_API void HandleSelectionChanged(TConstArrayView<FGuid> Selection);
	UE_API void HandleSlotsChanged(USmartObjectDefinition* Definition);

	UE_API FReply OnOutlinerDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;
	UE_API TOptional<EItemDropZone> OnOutlinerCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, const TSharedPtr<FSmartObjectOutlinerItem> TargetItem) const;
	UE_API FReply OnOutlinerAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone,  const TSharedPtr<FSmartObjectOutlinerItem> TargetItem);

	UE_API void UpdateDetailsSelection();

	UE_API FText GetOutlinerItemDescription(TSharedPtr<FSmartObjectOutlinerItem> Item) const;
	UE_API FSlateColor GetOutlinerItemColor(TSharedPtr<FSmartObjectOutlinerItem> Item) const;
	
	/** Additional Tab to select mesh/actor to add a 3D preview in the scene. */
	static UE_API const FName PreviewSettingsTabID;
	static UE_API const FName OutlinerTabID;
	static UE_API const FName SceneViewportTabID;
	static UE_API const FName DetailsTabID;

	/** Scene in which the 3D preview of the asset lives. */
	TUniquePtr<FAdvancedPreviewScene> AdvancedPreviewScene;

	/** Details view for the preview settings. */
	TSharedPtr<IStructureDetailsView> PreviewDetailsView;

	TSharedPtr<FStructOnScope> CachedPreviewData;
	
	TSharedPtr<STreeView<TSharedPtr<FSmartObjectOutlinerItem>>> ItemTreeWidget;
	TArray<TSharedPtr<FSmartObjectOutlinerItem>> ItemList;
	bool bUpdatingOutlinerSelection = false;
	bool bUpdatingViewSelection = false;

	TSharedPtr<SDockTab> DetailsTab;
	TSharedPtr<IDetailsView> DetailsAssetView;
	
    /** Typed pointer to the custom ViewportClient created by the toolkit. */
	mutable TSharedPtr<class FSmartObjectAssetEditorViewportClient> SmartObjectViewportClient;

	TSharedPtr<FSmartObjectViewModel> ViewModel;
	
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SlotsChangedHandle;
};

#undef UE_API
