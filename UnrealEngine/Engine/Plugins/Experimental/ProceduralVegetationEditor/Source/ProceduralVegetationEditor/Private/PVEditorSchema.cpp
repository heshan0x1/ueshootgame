// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVEditorSchema.h"
#include "Nodes/PVBaseSettings.h"

EPCGElementType UPVEditorSchema::GetElementTypeFiltering() const
{
	return EPCGElementType::Native;
}

const FPCGGraphEditorCustomization& UPVEditorSchema::GetGraphEditorCustomization(const UEdGraph* InEdGraph) const
{
	static const FPCGGraphEditorCustomization GraphEditorCustomization
	{
		.bFilterSettings = true,
		.FilteredSettingsTypes =
	{
			UPVBaseSettings::StaticClass()
		}
	};

	return GraphEditorCustomization;
}
