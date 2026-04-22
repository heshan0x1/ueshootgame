// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/MutableMaterialParameterNode.h"
#include "Materials/MaterialInterface.h"

FMutableMaterialParameterNode::FMutableMaterialParameterNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&ParameterName);
	RegisterInputConnection(&Material);

	RegisterOutputConnection(&MaterialParameter);
}


void FMutableMaterialParameterNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FMutableMaterialParameter Output;
	Output.Name = GetValue(Context, &ParameterName);
	Output.Material = GetValue(Context, &Material);
	
	if (Output.Name.IsEmpty())
	{
		Context.Warning(TEXT("The generated Material Parameter is empty. Make sure the parameter has a name."), this);
	}
	
	SetValue(Context, Output, &MaterialParameter);
}
