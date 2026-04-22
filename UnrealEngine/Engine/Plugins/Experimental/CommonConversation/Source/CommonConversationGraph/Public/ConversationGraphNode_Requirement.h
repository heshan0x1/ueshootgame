// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConversationGraphNode.h"
#include "ConversationGraphNode_Requirement.generated.h"

#define UE_API COMMONCONVERSATIONGRAPH_API

UCLASS(MinimalAPI)
class UConversationGraphNode_Requirement : public UConversationGraphNode
{
	GENERATED_UCLASS_BODY()

	UE_API virtual void AllocateDefaultPins() override;
	UE_API virtual FLinearColor GetNodeBodyTintColor() const override;
};

#undef UE_API
