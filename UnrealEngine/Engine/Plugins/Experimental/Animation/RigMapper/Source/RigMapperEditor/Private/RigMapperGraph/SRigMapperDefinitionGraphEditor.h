// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GraphEditor.h"
#include "Widgets/SCompoundWidget.h"

#define UE_API RIGMAPPEREDITOR_API

class URigMapperDefinitionEditorGraphNode;
class URigMapperDefinition;
class URigMapperDefinitionEditorGraph;

/**
 * 
 */
class SRigMapperDefinitionGraphEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRigMapperDefinitionGraphEditor)
		{
		}

	SLATE_END_ARGS()

	UE_API virtual ~SRigMapperDefinitionGraphEditor() override;

	/** Constructs this widget with InArgs */
	UE_API void Construct(const FArguments& InArgs, URigMapperDefinition* InDefinition);

	UE_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	UE_API void SelectNodes(const TArray<FString>& Inputs, const TArray<FString>& Features, const TArray<FString>& Outputs, const TArray<FString>& NullOutputs);
	UE_API void RebuildGraph();

private:
	UE_API void ZoomToFitNodes(const TArray<URigMapperDefinitionEditorGraphNode*>& SelectedNodes) const;
	static UE_API void GetAllLinkedNodes(const URigMapperDefinitionEditorGraphNode* BaseNode, TArray<URigMapperDefinitionEditorGraphNode*>& LinkedNodes, bool bDescend);
	UE_API void HandleSelectionChanged(const TSet<UObject*>& Nodes);

public:
	SGraphEditor::FOnSelectionChanged OnSelectionChanged;
	
private:
	TSharedPtr<SGraphEditor> GraphEditor;
	TObjectPtr<URigMapperDefinitionEditorGraph> GraphObj;

	bool bSelectingNodes = false;

	bool bFocusLinkedNodes = true;

	bool bDeferZoom = false;
};

#undef UE_API
