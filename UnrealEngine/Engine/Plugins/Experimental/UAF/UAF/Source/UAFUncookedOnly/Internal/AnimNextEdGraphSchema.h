// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/RigVMEdGraphSchema.h"
#include "AnimNextEdGraphNode.h"
#include "AnimNextEdGraphSchema.generated.h"

class IAssetReferenceFilter;

#define UE_API UAFUNCOOKEDONLY_API

UCLASS(MinimalAPI)
class UAnimNextEdGraphSchema : public URigVMEdGraphSchema
{
	GENERATED_BODY()

	// URigVMEdGraphSchema interface
	virtual TSubclassOf<URigVMEdGraphNode> GetGraphNodeClass(const URigVMEdGraph* InGraph) const override { return UAnimNextEdGraphNode::StaticClass(); }

	// UEdGraphSchema interface
	UE_API virtual void GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const override;

protected:
	UE_API static TSharedPtr<IAssetReferenceFilter> MakeAssetReferenceFilter(const UEdGraph* Graph);
};

#undef UE_API