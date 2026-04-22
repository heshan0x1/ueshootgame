// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTranslator.h"
#include "MassCommonTypes.h"
#include "MassEntityQuery.h"

//----------------------------------------------------------------------//
//  UMassTranslator
//----------------------------------------------------------------------//

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassTranslator)
UMassTranslator::UMassTranslator()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMassTranslator::AddRequiredTagsToQuery(FMassEntityQuery& EntityQuery)
{
	EntityQuery.AddTagRequirements<EMassFragmentPresence::All>(RequiredTags);
}
