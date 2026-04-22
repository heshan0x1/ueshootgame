// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeFixup.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteDefinitions.h"
#include "NaniteEncodeShared.h"
#include "Nanite/NaniteFixupChunk.h"


namespace Nanite
{

void CalculatePageDependenciesAndFixups(
	FResources& Resources,
	TArray<FPageFixups>& PageFixups,
	const TArray<FPage>& Pages,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts
)
{
	const TArray<FCluster>& Clusters = ClusterDAG.Clusters;
	const TArray<FClusterGroup>& Groups = ClusterDAG.Groups;
	const uint32 NumPages = Pages.Num();

	PageFixups.SetNum(NumPages);

	struct FPartFixupRef
	{
		uint32 PageIndex		= MAX_uint32;
		uint32 PartFixupIndex	= MAX_uint32;
	};

	TArray<TArray<uint32, TInlineAllocator<8>>> GroupToParts;
	TArray<TArray<uint32, TInlineAllocator<16>>> GroupToParentParts;
	TArray<TArray<uint16, TInlineAllocator<16>>> PageDependencies;
	TArray<FPartFixupRef> PartToPartFixup;
	
	GroupToParts.SetNum(Groups.Num());
	GroupToParentParts.SetNum(Groups.Num());
	PageDependencies.SetNum(Pages.Num());
	PartToPartFixup.SetNum(Parts.Num());

	// Build GroupToParts and GroupToParentParts
	for (uint32 PartIndex = 0; PartIndex < (uint32)Parts.Num(); PartIndex++)
	{
		const FClusterGroupPart& Part = Parts[PartIndex];
		check(Part.MinLODError >= 0.0f);

		GroupToParts[Part.GroupIndex].Add(PartIndex);

		for (uint32 ClusterIndex : Part.Clusters)
		{
			const FCluster& Cluster = Clusters[ClusterIndex];
			if (Cluster.GeneratingGroupIndex != MAX_uint32)
			{
				GroupToParentParts[Cluster.GeneratingGroupIndex].AddUnique(PartIndex);
			}
		}
	}

	// Note: Because of assembly references, we can't assume we can go from child parts to parents as not all groups have parts.
	//       But we can always go from parent part to child groups, or child group to parent parts.

	
	for (uint32 GroupIndex = 0; GroupIndex < (uint32)Groups.Num(); GroupIndex++)
	{
		const FClusterGroup& Group = Groups[GroupIndex];
		if (Group.bTrimmed)
			continue;

		const uint32 HierarchyRootOffset = Resources.HierarchyRootOffsets[Group.MeshIndex];

		// Last page owns the fixup for the entire group
		uint32 LastPageIndex = 0;
		Resources.ForEachPage(Group.PageRangeKey, [&LastPageIndex](uint32 PageIndex)
			{
				LastPageIndex = FMath::Max(LastPageIndex, PageIndex);
			});

		Resources.ForEachPage(Group.PageRangeKey, [&](uint32 PageIndex)
			{
				check(PageIndex <= LastPageIndex);

				// Other pages in range reference the last page for reconsideration
				if (PageIndex < LastPageIndex && !Resources.IsRootPage(PageIndex))
				{
					PageFixups[PageIndex].ReconsiderPages.AddUnique(uint16(LastPageIndex));
				}

				// Make every page of the group depend on all pages from parent part groups
				for (uint32 ParentPartIndex : GroupToParentParts[GroupIndex])
				{
					const FClusterGroup& ParentGroup = Groups[Parts[ParentPartIndex].GroupIndex];
					Resources.ForEachPage(ParentGroup.PageRangeKey, [&](uint32 ParentPageIndex)
						{
							if (ParentPageIndex != PageIndex)
							{
								PageDependencies[PageIndex].AddUnique(uint16(ParentPageIndex));
							}
						});
				}
			});

		FPageFixups& LastPageFixups			= PageFixups[LastPageIndex];
		FGroupFixup& GroupFixup				= LastPageFixups.GroupFixups.AddDefaulted_GetRef();
		GroupFixup.PageDependencyRangeKey	= Group.PageRangeKey;
		GroupFixup.Flags					= 0u;
		GroupFixup.FirstPartFixup			= LastPageFixups.PartFixups.Num();
		GroupFixup.GroupIndex				= GroupIndex;

		// Add fixups for all group parts
		for (uint32 PartIndex : GroupToParts[GroupIndex])
		{
			const FClusterGroupPart& Part = Parts[PartIndex];
			check(Part.GroupIndex == GroupIndex);
			check(PartToPartFixup[PartIndex].PageIndex == MAX_uint32);

			PartToPartFixup[PartIndex]	= { LastPageIndex, uint32(LastPageFixups.PartFixups.Num()) };

			FPartFixup& PartFixup		= LastPageFixups.PartFixups.AddDefaulted_GetRef();
			PartFixup.PageIndex			= Part.PageIndex;
			PartFixup.StartClusterIndex = Part.PageClusterOffset;
			PartFixup.LeafCounter		= 0u;
			PartFixup.GroupIndex		= GroupIndex;

			// It needs to be installed for every assembly instance
			for (const FHierarchyNodeRef& NodeRef : Part.HierarchyNodeRefs)
			{
				const uint32 GlobalHierarchyNodeIndex = HierarchyRootOffset + NodeRef.NodeIndex;
				PartFixup.HierarchyLocations.Add(FHierarchyNodeRef{ GlobalHierarchyNodeIndex, NodeRef.ChildIndex });
			}
		}
		GroupFixup.NumPartFixups = LastPageFixups.PartFixups.Num() - GroupFixup.FirstPartFixup;
	}
	
	uint32 NumAddedPages = 0;
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		for (FGroupFixup& GroupFixup : PageFixups[PageIndex].GroupFixups)
		{
			const uint32 GroupIndex = GroupFixup.GroupIndex;
			const FClusterGroup& Group = Groups[GroupIndex];

			uint32 LastPageIndex = 0;
			Resources.ForEachPage(Group.PageRangeKey, [&LastPageIndex](uint32 PageIndex)
				{
					LastPageIndex = FMath::Max(LastPageIndex, PageIndex);
				});

			if (!Resources.IsRootPage(LastPageIndex))
			{
				// Add fixup information to update parent parts
				for (uint32 ParentPartIndex : GroupToParentParts[GroupIndex])
				{
					const FClusterGroupPart& ParentPart = Parts[ParentPartIndex];

					FParentFixup& ParentPartFixup = GroupFixup.ParentFixups.AddDefaulted_GetRef();
					ParentPartFixup.PageIndex = ParentPart.PageIndex;
					ParentPartFixup.PartFixupPageIndex = PartToPartFixup[ParentPartIndex].PageIndex;
					ParentPartFixup.PartFixupIndex = PartToPartFixup[ParentPartIndex].PartFixupIndex;

					check(ParentPartFixup.PartFixupPageIndex == PageIndex || PageDependencies[PageIndex].Contains(ParentPartFixup.PartFixupPageIndex));
					
					PageFixups[ParentPartFixup.PartFixupPageIndex].PartFixups[ParentPartFixup.PartFixupIndex].LeafCounter++;

					// Add parent cluster indices to change leaf status of
					for (uint32 i = 0; i < (uint32)ParentPart.Clusters.Num(); i++)
					{
						if (Clusters[ParentPart.Clusters[i]].GeneratingGroupIndex == GroupIndex)
						{
							const uint32 LocalClusterIndex = ParentPart.PageClusterOffset + i;
							check(LocalClusterIndex <= MAX_uint8);
							ParentPartFixup.ClusterIndices.Add(uint8(LocalClusterIndex));
						}
					}
					check(ParentPartFixup.ClusterIndices.Num());
				}
			}
		}
	}

	// Negate MinLODError of any part references that is not a leaf in the initial state
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		for (const FPartFixup& PartFixup : PageFixups[PageIndex].PartFixups)
		{
			if (PartFixup.LeafCounter > 0)
			{
				for (const FHierarchyNodeRef& NodeRef : PartFixup.HierarchyLocations)
				{
					Resources.HierarchyNodes[NodeRef.NodeIndex].Misc0[NodeRef.ChildIndex].MinLODError_MaxParentLODError |= 0x80000000u;	// MinLODError <= -0.0f
				}
			}
		}
	}

	// Generate page dependencies
	Resources.PageStreamingStates.SetNum(NumPages);
	Resources.NumClusters = 0;
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const FPage& Page = Pages[PageIndex];
		Resources.NumClusters += Page.NumClusters;

		FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];
		PageStreamingState.DependenciesStart = Resources.PageDependencies.Num();

		Resources.PageDependencies.Append(PageDependencies[PageIndex]);
		PageStreamingState.DependenciesNum = uint16(Resources.PageDependencies.Num() - PageStreamingState.DependenciesStart);
	}

	check(Resources.NumClusters <= (uint32)Clusters.Num());	// There can be unused clusters when trim is used

#if DO_CHECK
	// Root pages should only have information about how to install itself in the hierarchy.
	for (uint32 PageIndex = 0; PageIndex < Resources.NumRootPages; PageIndex++)
	{
		const FPageFixups& Fixups = PageFixups[PageIndex];
		for (const FGroupFixup& GroupFixup : Fixups.GroupFixups)
		{
			check(!GroupFixup.PageDependencyRangeKey.HasStreamingPages());

			check(GroupFixup.ParentFixups.Num() == 0);
		}
	}
#endif
}

void PerformPageInternalFixup(
	const FResources& Resources,
	const TArray<FPage>& Pages,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts,
	uint32 PageIndex,
	TArray<FPackedCluster>& PackedClusters)
{
	const FPage& Page = Pages[PageIndex];

	// Perform page-internal fix up directly on PackedClusters
	for (uint32 LocalPartIndex = 0; LocalPartIndex < Page.PartsNum; LocalPartIndex++)
	{
		const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + LocalPartIndex];
		const FClusterGroup& Group = ClusterDAG.Groups[Part.GroupIndex];
		const bool bRootGroup = !Group.PageRangeKey.HasStreamingPages();

		for (uint32 ClusterPositionInPart = 0; ClusterPositionInPart < (uint32)Part.Clusters.Num(); ClusterPositionInPart++)
		{
			const FCluster& Cluster = ClusterDAG.Clusters[Part.Clusters[ClusterPositionInPart]];
			FPackedCluster& PackedCluster = PackedClusters[Part.PageClusterOffset + ClusterPositionInPart];
			uint32 ClusterFlags = PackedCluster.GetFlags();

			if (bRootGroup)
			{
				ClusterFlags |= NANITE_CLUSTER_FLAG_ROOT_GROUP;
			}

			if (Cluster.GeneratingGroupIndex != MAX_uint32)
			{
				const FClusterGroup& GeneratingGroup = ClusterDAG.Groups[Cluster.GeneratingGroupIndex];

				bool bContainsCurrentPage = false;
				bool bAllDependenciesResident = Resources.TrueForAllPages(
					GeneratingGroup.PageRangeKey,
					[&](uint32 PageIndex)
					{
						if (PageIndex == Cluster.PageIndex)
						{
							bContainsCurrentPage = true;
							return true;
						}
						return false; // contains a streaming page that isn't the current
					},
					true // bStreamingPagesOnly
				);

				if (bAllDependenciesResident)
				{
					// Dependencies met by current page and/or root pages
					ClusterFlags &= ~NANITE_CLUSTER_FLAG_STREAMING_LEAF;
					
					if (!bContainsCurrentPage)
					{
						// Dependencies met by root pages
						ClusterFlags &= ~NANITE_CLUSTER_FLAG_ROOT_LEAF;
					}
				}
			}
			else
			{
				ClusterFlags |= NANITE_CLUSTER_FLAG_FULL_LEAF;
			}
			PackedCluster.SetFlags(ClusterFlags);
		}
	}
}

void BuildFixupChunkData(TArray<uint8>& OutData, const FPageFixups& PageFixups, uint32 NumClusters)
{
	const uint32 TotalGroupFixups	= (uint32)PageFixups.GroupFixups.Num();
	uint32 TotalPartFixups			= (uint32)PageFixups.PartFixups.Num();
	uint32 TotalParentFixups		= 0u;
	uint32 TotalHierarchyLocations	= 0u;
	uint32 TotalClusterIndices		= 0u;

	for (const FGroupFixup& GroupFixup : PageFixups.GroupFixups)
	{
		TotalParentFixups += GroupFixup.ParentFixups.Num();

		for (const FParentFixup& ParentFixup : GroupFixup.ParentFixups)
		{
			TotalClusterIndices += ParentFixup.ClusterIndices.Num();
		}
	}

	for (const FPartFixup& PartFixup : PageFixups.PartFixups)
	{
		TotalHierarchyLocations += PartFixup.HierarchyLocations.Num();
	}

	const uint32 FixupChunkSize = FFixupChunk::GetSize(TotalGroupFixups, TotalPartFixups, TotalParentFixups, TotalHierarchyLocations, PageFixups.ReconsiderPages.Num(), TotalClusterIndices);
	OutData.Init(0x00, FixupChunkSize);

	FFixupChunk& FixupChunk					= *(FFixupChunk*)OutData.GetData();
	FixupChunk.Header.Magic					= NANITE_FIXUP_MAGIC;
	FixupChunk.Header.NumGroupFixups		= IntCastChecked<uint16>(TotalGroupFixups);
	FixupChunk.Header.NumPartFixups			= IntCastChecked<uint16>(TotalPartFixups);
	FixupChunk.Header.NumClusters			= IntCastChecked<uint16>(NumClusters);
	FixupChunk.Header.NumReconsiderPages	= IntCastChecked<uint16>(PageFixups.ReconsiderPages.Num());
	FixupChunk.Header.NumParentFixups		= TotalParentFixups;
	FixupChunk.Header.NumHierarchyLocations	= TotalHierarchyLocations;
	FixupChunk.Header.NumClusterIndices		= TotalClusterIndices;
	
	uint32 NextGroupOffset		= 0u;
	uint32 NextPartOffset		= 0u;
	uint32 NextParentOffset		= 0u;
	uint32 NextHierarchyOffset	= 0u;
	uint32 NextClusterOffset	= 0u;

	for (const FGroupFixup& SrcGroupFixup : PageFixups.GroupFixups)
	{
		FFixupChunk::FGroupFixup& GroupFixup = FixupChunk.GetGroupFixup(NextGroupOffset++);
		GroupFixup.PageDependencies = SrcGroupFixup.PageDependencyRangeKey;
		GroupFixup.Flags			= 0u;
		GroupFixup.FirstPartFixup	= IntCastChecked<uint16>(SrcGroupFixup.FirstPartFixup);
		GroupFixup.NumPartFixups	= IntCastChecked<uint16>(SrcGroupFixup.NumPartFixups);
		GroupFixup.FirstParentFixup	= IntCastChecked<uint16>(NextParentOffset);
		GroupFixup.NumParentFixups	= IntCastChecked<uint16>(SrcGroupFixup.ParentFixups.Num());

		for (const FParentFixup& SrcParentFixup : SrcGroupFixup.ParentFixups)
		{
			FFixupChunk::FParentFixup& ParentFixup = FixupChunk.GetParentFixup(NextParentOffset++);
			
			ParentFixup.PageIndex				= IntCastChecked<uint16>(SrcParentFixup.PageIndex);
			ParentFixup.PartFixupPageIndex		= IntCastChecked<uint16>(SrcParentFixup.PartFixupPageIndex);
			ParentFixup.PartFixupIndex			= IntCastChecked<uint8>(SrcParentFixup.PartFixupIndex);

			ParentFixup.NumClusterIndices		= IntCastChecked<uint8>(SrcParentFixup.ClusterIndices.Num());
			ParentFixup.FirstClusterIndex		= IntCastChecked<uint16>(NextClusterOffset);

			for (uint8 ClusterIndex : SrcParentFixup.ClusterIndices)
			{
				FixupChunk.GetClusterIndex(NextClusterOffset++) = ClusterIndex;
			}
		}
	}

	for (const FPartFixup& SrcPartFixup : PageFixups.PartFixups)
	{
		FFixupChunk::FPartFixup& PartFixup	= FixupChunk.GetPartFixup(NextPartOffset++);
		
		PartFixup.PageIndex					= IntCastChecked<uint16>(SrcPartFixup.PageIndex);
		PartFixup.StartClusterIndex			= IntCastChecked<uint8>(SrcPartFixup.StartClusterIndex);
		PartFixup.LeafCounter				= IntCastChecked<uint8>(SrcPartFixup.LeafCounter);
		PartFixup.FirstHierarchyLocation	= NextHierarchyOffset;
		PartFixup.NumHierarchyLocations		= IntCastChecked<uint16>(SrcPartFixup.HierarchyLocations.Num());

		for (const FHierarchyNodeRef& NodeRef : SrcPartFixup.HierarchyLocations)
		{
			FixupChunk.GetHierarchyLocation(NextHierarchyOffset++) = FFixupChunk::FHierarchyLocation(NodeRef.NodeIndex, NodeRef.ChildIndex);
		}
	}


	for (int32 i = 0; i < PageFixups.ReconsiderPages.Num(); i++)
	{
		FixupChunk.GetReconsiderPageIndex(i) = PageFixups.ReconsiderPages[i];
	}

	check(NextGroupOffset == TotalGroupFixups);
	check(NextPartOffset == TotalPartFixups);
	check(NextParentOffset == TotalParentFixups);
	check(NextHierarchyOffset == TotalHierarchyLocations);
	check(NextClusterOffset == TotalClusterIndices);
}

} // namespace Nanite