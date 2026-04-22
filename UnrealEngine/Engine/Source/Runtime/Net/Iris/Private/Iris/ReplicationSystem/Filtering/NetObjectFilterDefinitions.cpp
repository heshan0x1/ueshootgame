// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/Filtering/NetObjectFilterDefinitions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectFilterDefinitions)

TConstArrayView<FNetObjectFilterDefinition> UNetObjectFilterDefinitions::GetFilterDefinitions() const
{
	return MakeArrayView(NetObjectFilterDefinitions);
}
