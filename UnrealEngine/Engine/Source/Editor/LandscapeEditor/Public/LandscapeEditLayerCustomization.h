// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
class ULandscapeEditLayerBase;
class FMenuBuilder;
struct FMenuEntryParams;

struct FEditLayerMenuBlock
{
	TArray<FMenuEntryParams> Entries;
	FText SectionLabel;
};

/* Map of context menu Category Names to their entries */
using FEditLayerCategoryToEntryMap = TMap<FName, FEditLayerMenuBlock>;

/**
 * Interface for Edit Layer classes that expand on customizations not covered by IDetailsView (Context Menu, Dialog, etc.)
 */
class IEditLayerCustomization : public TSharedFromThis<IEditLayerCustomization>
{
public:
	virtual ~IEditLayerCustomization() {}

	/** Called when building edit layer context menus */
	virtual void CustomizeContextMenu(ULandscapeEditLayerBase* InEditLayer, FEditLayerCategoryToEntryMap& OutContextMenuEntryMap) = 0;
};
