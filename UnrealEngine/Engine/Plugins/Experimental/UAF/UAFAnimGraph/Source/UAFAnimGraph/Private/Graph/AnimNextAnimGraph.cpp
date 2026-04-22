// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextAnimGraph.h"
#include "Graph/AnimNextGraphInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextAnimGraph)

bool FAnimNextAnimGraph::IsEqualForInjectionSiteChange(const FAnimNextAnimGraph& InOther) const
{
	return Asset == InOther.Asset &&
			HostGraph == InOther.HostGraph;
}
