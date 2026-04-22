// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

struct FResources;
class FClusterDAG;
struct FPage;
struct FClusterGroupPart;

void BuildHierarchies(
	FResources& Resources,
	const FClusterDAG& ClusterDAG,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts,
	uint32 NumMeshes
);

void CalculateFinalPageHierarchyDepth(const FResources& Resources, TArray<FPage>& Pages);

}
