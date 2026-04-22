// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/MutableTextureParameterNode.h"
#include "Engine/Texture2D.h"

FMutableTextureParameterNode::FMutableTextureParameterNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&ParameterName);
	RegisterInputConnection(&Texture);

	RegisterOutputConnection(&TextureParameter);
}


void FMutableTextureParameterNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	FMutableTextureParameter Output;
	Output.Name =  GetValue(Context, &ParameterName);
	Output.Texture = GetValue(Context, &Texture);
	
	if (Output.Name.IsEmpty())
	{
		Context.Warning(TEXT("The generated Texture Parameter is empty. Make sure the parameter has a name."), this);
	}

	SetValue(Context, Output, &TextureParameter);
}