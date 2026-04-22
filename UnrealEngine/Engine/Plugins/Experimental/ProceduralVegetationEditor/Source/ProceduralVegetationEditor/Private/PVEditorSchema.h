// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGEditor.h"
#include "PVEditorSchema.generated.h"

UCLASS()
class UPVEditorSchema : public UPCGEditorGraphSchema
{
	GENERATED_BODY()

public:
	// Return the element type filtering for the current editor. By default it is all.
	virtual EPCGElementType GetElementTypeFiltering() const override;

	// Return the graph customization for this editor. By default it is the one provided by the graph.
	virtual const FPCGGraphEditorCustomization& GetGraphEditorCustomization(const UEdGraph* InEdGraph) const override;
};
