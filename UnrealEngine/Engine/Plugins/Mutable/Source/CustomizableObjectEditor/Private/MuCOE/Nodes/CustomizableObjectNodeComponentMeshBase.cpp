// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshBase.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeComponentMeshBase)


TArray<FEdGraphPinReference>& ICustomizableObjectNodeComponentMeshInterface::GetLODPins()
{
	const ICustomizableObjectNodeComponentMeshInterface* ConstThis = this;
	return const_cast<TArray<FEdGraphPinReference>&>(ConstThis->GetLODPins());
}
