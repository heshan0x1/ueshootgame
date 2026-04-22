// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/NaniteResources.h"
#include "NaniteEncodeShared.h"

namespace Nanite
{

struct FResources;
class FClusterFixup;
class FHierarchyFixup;
struct FPage;
class FClusterDAG;
struct FClusterGroupPart;
struct FPackedCluster;

struct FPartFixup
{
	uint32						PageIndex			= MAX_uint32;
	uint32						StartClusterIndex	= MAX_uint32;
	uint32						LeafCounter			= 0u; // 0 means part has no leaf clusters
	
	TArray<FHierarchyNodeRef>	HierarchyLocations;	// Assembly parts can be referenced from many hierarchy nodes

	// Builder only
	uint32					GroupIndex = MAX_uint32;
};

struct FParentFixup
{
	uint32						PageIndex			= MAX_uint32;
	uint32						PartFixupPageIndex	= MAX_uint32;
	uint32						PartFixupIndex		= MAX_uint32;
	TArray<uint8>				ClusterIndices;		// Clusters to change leaf status of
};

// Group fixups are always stored in the LAST page.
struct FGroupFixup
{
	FPageRangeKey			PageDependencyRangeKey;
	uint32					Flags					= 0;
	uint32					FirstPartFixup			= MAX_uint32;
	uint32					NumPartFixups			= MAX_uint32;

	TArray<FParentFixup>	ParentFixups;

	// Builder only
	uint32					GroupIndex = MAX_uint32;
};

struct FPageFixups
{
	TArray<FGroupFixup>		GroupFixups;
	TArray<FPartFixup>		PartFixups;
	TArray<uint16>			ReconsiderPages;
};

void CalculatePageDependenciesAndFixups(
	FResources& Resources,
	TArray<FPageFixups>& PageFixups,
	const TArray<FPage>& Pages,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts);

void PerformPageInternalFixup(
	const FResources& Resources,
	const TArray<FPage>& Pages,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts,
	uint32 PageIndex,
	TArray<FPackedCluster>& PackedClusters);

void BuildFixupChunkData(TArray<uint8>& OutData, const FPageFixups& PageFixups, uint32 NumClusters);

}
