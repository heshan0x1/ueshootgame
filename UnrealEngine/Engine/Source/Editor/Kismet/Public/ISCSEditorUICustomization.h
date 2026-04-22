// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ChildActorComponent.h"
#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Containers/ArrayView.h"

class UActorComponent;
struct FSubobjectData;
struct FSubobjectDataHandle;

/** SCSEditor UI customization */
class ISCSEditorUICustomization
{
public:
	virtual ~ISCSEditorUICustomization() {}

	UE_DEPRECATED(5.5, "Use version with ObjectContext instead")
	virtual bool HideComponentsTree() const { return false; }

	UE_DEPRECATED(5.5, "Use version with ObjectContext instead")
	virtual bool HideComponentsFilterBox() const { return false; }

	UE_DEPRECATED(5.5, "Use version with ObjectContext instead")
	virtual bool HideAddComponentButton() const { return false; }

	UE_DEPRECATED(5.5, "Use version with ObjectContext instead")
	virtual bool HideBlueprintButtons() const { return false; }

	UE_DEPRECATED(5.5, "Use version with ObjectContext instead")
	virtual TSubclassOf<UActorComponent> GetComponentTypeFilter() const { return nullptr; }

	/** @return Whether to hide the components tree */
	virtual bool HideComponentsTree(TArrayView<UObject*> Context) const { return false; }

	/** @return Whether to hide the components filter box */
	virtual bool HideComponentsFilterBox(TArrayView<UObject*> Context) const { return false; }

	/** @return Whether to hide the "Add Component" combo button */
	virtual bool HideAddComponentButton(TArrayView<UObject*> Context) const { return false; }

	/** @return Whether to hide the "Edit Blueprint" and "Blueprint/Add Script" buttons */
	virtual bool HideBlueprintButtons(TArrayView<UObject*> Context) const { return false; }

	/** @return The icon that should be used for the given SubobjectData
	 * 
	 * Implementations that return nullptr will lead to existing built-in functionality of the
	 * subobject editor being used to determine the Icon for the given FSubobjectData 
	 */
	virtual const FSlateBrush* GetIconBrush(const FSubobjectData&) const { return nullptr; }

	/** Allows customizer to inject a right-side justified widget on the subobject editor
	 * Typically these widgets would convey actions that can be taken and may optionally provide
	 * status information through their visualization.
	 * As an example, a subobject that is a non-native ActorComponent may provide a button via these widgets
	 * to open a code editor to the C++ version of that type.
	 * Returning nullptr will cause built-in behaviour to be used, indicating that the UICustomization does not customize this
	 * UI element. To override existing behaviour and actually show nothing, return a single null widget
	 */
	virtual TSharedPtr<SWidget> GetControlsWidget(const FSubobjectData&) const { return {}; }

	/** 
	 * @return An override for the default ChildActorComponentTreeViewVisualizationMode from the project settings, if different from UseDefault.
	 * @note Setting an override also forces child actor tree view expansion to be enabled.
	 */
	virtual EChildActorComponentTreeViewVisualizationMode GetChildActorVisualizationMode() const { return EChildActorComponentTreeViewVisualizationMode::UseDefault; }

	/** @return A component type that limits visible nodes when filtering the tree view */
	virtual TSubclassOf<UActorComponent> GetComponentTypeFilter(TArrayView<UObject*> Context) const { return nullptr; }

	/** Optionally sorts the given SubobjectDataHandles for the tree view as appropriate for this editor.
	 * @return Whether any sorting was attempted */
	virtual bool SortSubobjectData(TArray<FSubobjectDataHandle>& SubobjectData) { return false; }
};
