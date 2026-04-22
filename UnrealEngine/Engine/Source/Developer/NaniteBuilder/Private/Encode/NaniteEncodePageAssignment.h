// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

class FClusterDAG;
struct FEncodingInfo;
struct FPage;
struct FClusterGroupPart;
struct FPageRangeKey;

void AssignClustersToPages(
	FClusterDAG& ClusterDAG,
	TArray<FPageRangeKey>& PageRangeLookup,
	const TArray<FEncodingInfo>& EncodingInfos,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts,
	const uint32 MaxRootPages);

}
