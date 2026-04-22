// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/MutableSkeletalMeshParameterNode.h"
#include "Engine/SkeletalMesh.h"

FMutableSkeletalMeshParameterNode::FMutableSkeletalMeshParameterNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&ParameterName);
	RegisterInputConnection(&SkeletalMesh);

	RegisterOutputConnection(&SkeletalMeshParameter);
}


void FMutableSkeletalMeshParameterNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FMutableSkeletalMeshParameter Output;
	Output.Name = GetValue(Context, &ParameterName);
	Output.Mesh = GetValue(Context, &SkeletalMesh);

	if (Output.Name.IsEmpty())
	{
		Context.Warning(TEXT("The generated Skeletal Mesh Parameter is empty. Make sure the parameter has a name."), this);
	}

	SetValue(Context, Output, &SkeletalMeshParameter);
}
