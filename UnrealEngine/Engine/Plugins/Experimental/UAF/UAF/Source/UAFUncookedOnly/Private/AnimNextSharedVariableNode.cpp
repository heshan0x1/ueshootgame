// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextSharedVariableNode.h"
#include "Variables/AnimNextSharedVariables.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSharedVariableNode)

FString UAnimNextSharedVariableNode::GetNodeSubTitle() const
{
	switch (Type)
	{
	case EAnimNextSharedVariablesType::Asset:
		return Asset ? Asset->GetFName().ToString() : FString();
	case EAnimNextSharedVariablesType::Struct:
		return Struct ? Struct->GetDisplayNameText().ToString(): FString();
	}
	return FString();
}
