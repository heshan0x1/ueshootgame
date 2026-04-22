// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class ACompositeActor;
class IDetailsView;
class SCompositePanelLayerTree;

/**
 * Editor that displays the compositing actors in the level and displays the layer tree and layer properties for them
 */
class SCompositeEditorPanel : public SCompoundWidget
{
public:
	/** Registers the tab spawner for the panel with the editor */
	static void RegisterTabSpawner();

	/** Unregisters the tab spawner for the panel with the editor */
	static void UnregisterTabSpawner();
	
public:
	SLATE_BEGIN_ARGS(SCompositeEditorPanel) { }
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

	/** Selects the specified composite actors in the layer tree */
	void SelectCompositeActors(const TArray<TWeakObjectPtr<ACompositeActor>>& InCompositeActors);
	
private:
	/** Raised when the selected layers in the tree view have changed */
	void OnLayerSelectionChanged(const TArray<UObject*>& SelectedLayers);

	/** Gets the minimum height to display the full layer tree */
	float GetLayerTreeMinHeight() const;
	
private:
	/** The tree view widget that displays the composite layers */
	TSharedPtr<SCompositePanelLayerTree> LayerTree;

	/** The details view that displays the properties of the selected layers and actors */
	TSharedPtr<IDetailsView> DetailsView;
};
