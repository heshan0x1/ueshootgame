// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "RigMapperDefinitionEditorGraph.generated.h"

#define UE_API RIGMAPPEREDITOR_API

enum class ERigMapperNodeType : uint8;
class URigMapperDefinitionEditorGraphNode;
class URigMapperDefinition;

/**
 * 
 */
UCLASS(MinimalAPI)
class URigMapperDefinitionEditorGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()

public:
	UE_API void Initialize(URigMapperDefinition* InDefinition);
	
	UE_API void RebuildGraph();
	UE_API void ConstructNodes();
	UE_API void RemoveAllNodes();
	void RequestRefreshLayout(bool bInRefreshLayout) { bRefreshLayout = bInRefreshLayout; }
	bool NeedsRefreshLayout() const { return bRefreshLayout; }
	UE_API void LayoutNodes() const;
	
	URigMapperDefinition* GetDefinition() const { return WeakDefinition.Get(); };

	UE_API TArray<URigMapperDefinitionEditorGraphNode*> GetNodesByName(const TArray<FString>& Inputs, const TArray<FString>& Features, const TArray<FString>& Outputs, const TArray<FString>& NullOutputs) const;

private:
	UE_API URigMapperDefinitionEditorGraphNode* CreateGraphNodesRec(URigMapperDefinition* Definition, const FString& InNodeName, bool bIsOutputNode);
	UE_API URigMapperDefinitionEditorGraphNode* CreateOutputNode(URigMapperDefinition* Definition, const FString& NodeName);
	UE_API URigMapperDefinitionEditorGraphNode* CreateFeatureNode(URigMapperDefinition* Definition, const FString& NodeName);
	static UE_API void LinkGraphNodes(URigMapperDefinitionEditorGraphNode* InNode, URigMapperDefinitionEditorGraphNode* OutNode);
	UE_API URigMapperDefinitionEditorGraphNode* CreateGraphNode(const FString& NodeName, ERigMapperNodeType NodeType);
	UE_API void LayoutNodeRec(URigMapperDefinitionEditorGraphNode* InNode, double InputsWidth, double PosY, TArray<URigMapperDefinitionEditorGraphNode*>& LayedOutNodes) const;
	
private:
	TWeakObjectPtr<URigMapperDefinition> WeakDefinition;
	TMap<FString, URigMapperDefinitionEditorGraphNode*> InputNodes;
	TMap<FString, URigMapperDefinitionEditorGraphNode*> FeatureNodes;
	TMap<FString, URigMapperDefinitionEditorGraphNode*> OutputNodes;
	TMap<FString, URigMapperDefinitionEditorGraphNode*> NullOutputNodes;

	bool bRefreshLayout = false;
};

#undef UE_API
