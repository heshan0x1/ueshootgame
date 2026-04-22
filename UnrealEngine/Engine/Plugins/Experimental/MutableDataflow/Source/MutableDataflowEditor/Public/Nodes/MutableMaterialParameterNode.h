// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Dataflow/DataflowNode.h"
#include "Misc/Guid.h"
#include "CoreMinimal.h"
#include "MutableDataflowParameters.h"

#include "MutableMaterialParameterNode.generated.h"

USTRUCT(meta = (Experimental))
struct FMutableMaterialParameterNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMutableMaterialParameterNode, "MutableMaterialParameter", "Mutable", "")

	UPROPERTY(EditAnywhere, Category = "Mutable", meta = (DataflowInput))
	FString ParameterName;

	UPROPERTY(EditAnywhere, Category = "Mutable", meta = (DataflowInput))
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(meta = (DataflowOutput))
	FMutableMaterialParameter MaterialParameter;

public:
	
	FMutableMaterialParameterNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};