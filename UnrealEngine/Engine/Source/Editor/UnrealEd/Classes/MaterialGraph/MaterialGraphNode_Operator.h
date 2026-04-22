// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Base.h"

#include "MaterialGraphNode_Operator.generated.h"

class UEdGraphPin;

UCLASS(MinimalAPI)
class UMaterialGraphNode_Operator : public UMaterialGraphNode
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UEdGraphNode Interface.
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End UEdGraphNode Interface.
};