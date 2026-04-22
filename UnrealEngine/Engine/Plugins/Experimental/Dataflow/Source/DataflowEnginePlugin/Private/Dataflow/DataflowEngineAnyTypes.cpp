// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEngineAnyTypes.h"

#include "Dataflow/DataflowAnyTypeRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowEngineAnyTypes)

namespace UE::Dataflow
{
	void RegisterEngineAnyTypes()
	{
		UE_DATAFLOW_REGISTER_ANYTYPE(FDataflowDynamicMeshArray);
	}
}

