// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlendSpaceGraph.h"

#include "EdGraph/EdGraphSchema.h"
#include "Templates/SubclassOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BlendSpaceGraph)

void UBlendSpaceGraph::PostLoad()
{
	Super::PostLoad();

	// Fixup graph schema
	if(Schema && Schema != UEdGraphSchema::StaticClass())
	{
		Schema = UEdGraphSchema::StaticClass();
	}
}
