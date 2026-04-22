// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodePageAssignment.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteDefinitions.h"
#include "NaniteEncodeShared.h"

namespace Nanite
{

// Generate a permutation of cluster groups that is sorted first by mip level and then by Morton order x, y and z.
// Sorting by mip level first ensure that there can be no cyclic dependencies between formed pages.
static TArray<uint32> CalculateClusterGroupPermutation( const TArray< FClusterGroup >& ClusterGroups )
{
	struct FClusterGroupSortEntry {
		int32	AssemblyPartIndex;
		int32	MipLevel;
		uint32	MortonXYZ;
		uint32	OldIndex;
	};

	uint32 NumClusterGroups = ClusterGroups.Num();
	TArray< FClusterGroupSortEntry > ClusterGroupSortEntries;
	ClusterGroupSortEntries.SetNumUninitialized( NumClusterGroups );

	FVector3f MinCenter = FVector3f( FLT_MAX, FLT_MAX, FLT_MAX );
	FVector3f MaxCenter = FVector3f( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for( const FClusterGroup& ClusterGroup : ClusterGroups )
	{
		const FVector3f& Center = ClusterGroup.LODBounds.Center;
		MinCenter = FVector3f::Min( MinCenter, Center );
		MaxCenter = FVector3f::Max( MaxCenter, Center );
	}

	const float Scale = 1023.0f / (MaxCenter - MinCenter).GetMax();
	for( uint32 i = 0; i < NumClusterGroups; i++ )
	{
		const FClusterGroup& ClusterGroup = ClusterGroups[ i ];
		FClusterGroupSortEntry& SortEntry = ClusterGroupSortEntries[ i ];
		const FVector3f& Center = ClusterGroup.LODBounds.Center;
		const FVector3f ScaledCenter = ( Center - MinCenter ) * Scale + 0.5f;
		uint32 X = FMath::Clamp( (int32)ScaledCenter.X, 0, 1023 );
		uint32 Y = FMath::Clamp( (int32)ScaledCenter.Y, 0, 1023 );
		uint32 Z = FMath::Clamp( (int32)ScaledCenter.Z, 0, 1023 );

		SortEntry.AssemblyPartIndex = ClusterGroup.AssemblyPartIndex;
		SortEntry.MipLevel = ClusterGroup.MipLevel;
		SortEntry.MortonXYZ = ( FMath::MortonCode3(Z) << 2 ) | ( FMath::MortonCode3(Y) << 1 ) | FMath::MortonCode3(X);
		if ((ClusterGroup.MipLevel & 1) != 0)
		{
			SortEntry.MortonXYZ ^= 0xFFFFFFFFu;	// Alternate order so end of one level is near the beginning of the next
		}
		SortEntry.OldIndex = i;
	}

	ClusterGroupSortEntries.Sort( []( const FClusterGroupSortEntry& A, const FClusterGroupSortEntry& B ) {
		if (A.AssemblyPartIndex != B.AssemblyPartIndex)
			return A.AssemblyPartIndex < B.AssemblyPartIndex;
		if( A.MipLevel != B.MipLevel )
			return A.MipLevel > B.MipLevel;
		return A.MortonXYZ < B.MortonXYZ;
	} );

	TArray<uint32> Permutation;
	Permutation.SetNumUninitialized( NumClusterGroups );
	for( uint32 i = 0; i < NumClusterGroups; i++ )
		Permutation[ i ] = ClusterGroupSortEntries[ i ].OldIndex;
	return Permutation;
}

static void SortGroupClusters(FClusterDAG& DAG)
{
	for (FClusterGroup& Group : DAG.Groups)
	{
		FVector3f SortDirection = FVector3f(1.0f, 1.0f, 1.0f);
		Group.Children.Sort([&DAG, SortDirection](FClusterRef A, FClusterRef B) {
			const FCluster& ClusterA = A.GetCluster(DAG);
			const FCluster& ClusterB = B.GetCluster(DAG);
			float DotA = FVector3f::DotProduct(ClusterA.SphereBounds.Center, SortDirection);
			float DotB = FVector3f::DotProduct(ClusterB.SphereBounds.Center, SortDirection);
			return DotA < DotB;
		});
	}
}

static bool TryAddClusterToPage(FPage& Page, const FCluster& Cluster, const FEncodingInfo& EncodingInfo, bool bRootPage)
{
	FPage UpdatedPage = Page;

	UpdatedPage.NumClusters++;
	UpdatedPage.GpuSizes += EncodingInfo.GpuSizes;

	// Calculate sizes that don't just depend on the individual cluster
	if(Cluster.NumTris != 0)
	{
		UpdatedPage.MaxClusterBoneInfluences = FMath::Max(UpdatedPage.MaxClusterBoneInfluences, (uint32)EncodingInfo.BoneInfluence.ClusterBoneInfluences.Num());
	}
	else
	{
		UpdatedPage.MaxVoxelBoneInfluences = FMath::Max(UpdatedPage.MaxVoxelBoneInfluences, (uint32)EncodingInfo.BoneInfluence.VoxelBoneInfluences.Num());
	}
	
	UpdatedPage.GpuSizes.ClusterBoneInfluence = UpdatedPage.NumClusters * UpdatedPage.MaxClusterBoneInfluences * sizeof(FClusterBoneInfluence);
	UpdatedPage.GpuSizes.VoxelBoneInfluence = UpdatedPage.NumClusters * UpdatedPage.MaxVoxelBoneInfluences * sizeof(FPackedVoxelBoneInfluence);

	if (UpdatedPage.GpuSizes.GetTotal() <= (bRootPage ? NANITE_ROOT_PAGE_GPU_SIZE : NANITE_STREAMING_PAGE_GPU_SIZE) &&
		UpdatedPage.NumClusters <= (bRootPage ? NANITE_ROOT_PAGE_MAX_CLUSTERS : NANITE_STREAMING_PAGE_MAX_CLUSTERS))
	{
		Page = UpdatedPage;
		return true;
	}

	return false;
}

void AssignClustersToPages(
	FClusterDAG& ClusterDAG,
	TArray<FPageRangeKey>& PageRangeLookup,
	const TArray<FEncodingInfo>& EncodingInfos,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts,
	const uint32 MaxRootPages
	)
{
	check(Pages.Num() == 0);
	check(Parts.Num() == 0);
	
	TArray<FCluster>& Clusters = ClusterDAG.Clusters;
	TArray<FClusterGroup>& ClusterGroups = ClusterDAG.Groups;

	const uint32 NumClusterGroups = ClusterGroups.Num();
	Pages.AddDefaulted();

	SortGroupClusters(ClusterDAG);
	TArray<uint32> ClusterGroupPermutation = CalculateClusterGroupPermutation(ClusterGroups);
	TSet<uint32> GroupsWithInstances;

	for (uint32 i = 0; i < NumClusterGroups; i++)
	{
		// Pick best next group			// TODO
		uint32 GroupIndex = ClusterGroupPermutation[i];
		FClusterGroup& Group = ClusterGroups[GroupIndex];
		if( Group.bTrimmed )
			continue;

		uint32 GroupStartPage = MAX_uint32;

		bool bAllInstances = true;
	
		for (FClusterRef Child : Group.Children)
		{
			if( Child.IsInstance() )
			{
				GroupsWithInstances.Add(GroupIndex);
				continue;
			}

			bAllInstances = false;

			uint32 ClusterIndex = Child.ClusterIndex;

			// Pick best next cluster		// TODO
			FCluster& Cluster = Clusters[ClusterIndex];
			const FEncodingInfo& EncodingInfo = EncodingInfos[ClusterIndex];

			// Add to page
			FPage* Page = &Pages.Top();
			bool bRootPage =  (Pages.Num() - 1u) < MaxRootPages;

			// Try adding cluster to current page
			if (!TryAddClusterToPage(*Page, Cluster, EncodingInfo, bRootPage))
			{
				// Page is full. Start a new page.
				Pages.AddDefaulted();
				Page = &Pages.Top();
				bRootPage =  (Pages.Num() - 1u) < MaxRootPages;

				bool bResult = TryAddClusterToPage(*Page, Cluster, EncodingInfo, bRootPage);
				check(bResult);
			}
			
			// Start a new part?
			if (Page->PartsNum == 0 || Parts[Page->PartsStartIndex + Page->PartsNum - 1].GroupIndex != GroupIndex)
			{
				if (Page->PartsNum == 0)
				{
					Page->PartsStartIndex = Parts.Num();
				}
				Page->PartsNum++;

				FClusterGroupPart& Part = Parts.AddDefaulted_GetRef();
				Part.GroupIndex = GroupIndex;
			}

			// Add cluster to page
			uint32 PageIndex = Pages.Num() - 1;
			uint32 PartIndex = Parts.Num() - 1;

			FClusterGroupPart& Part = Parts.Last();
			if (Part.Clusters.Num() == 0)
			{
				Part.PageClusterOffset = Page->NumClusters - 1;
				Part.PageIndex = PageIndex;
			}
			Part.Clusters.Add(ClusterIndex);
			check(Part.Clusters.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP);

			Cluster.PageIndex = PageIndex;
			
			if (GroupStartPage == MAX_uint32)
			{
				GroupStartPage = PageIndex;
			}
		}

		if( bAllInstances )
		{
			// groups consisting entirely of instances are not assigned to a page
			check(GroupStartPage == MAX_uint32);
			continue;
		}

		const uint32 NumPages = Pages.Num() - GroupStartPage;
		check(NumPages >= 1);
		check(NumPages <= NANITE_MAX_GROUP_PARTS_MASK);

		const bool bHasStreamingPages = uint32(Pages.Num()) > MaxRootPages;
		Group.PageRangeKey = FPageRangeKey(GroupStartPage, NumPages, /* bMultiRange = */ false, bHasStreamingPages);
	}

	// Now all clusters' pages should be assigned, add instanced clusters pages to their page ranges
	{
		TArray<FPageRangeKey> PageRanges;
		TArray<uint32> InstancedPages;
		for (uint32 GroupIndex : GroupsWithInstances)
		{
			FClusterGroup& Group = ClusterGroups[GroupIndex];
			check(!Group.PageRangeKey.IsMultiRange()); // all groups should only have a single page range so far

			PageRanges.SetNum(0, EAllowShrinking::No);
			InstancedPages.SetNum(0, EAllowShrinking::No);

			for (FClusterRef Child : Group.Children)
			{
				if (Child.IsInstance())
				{
					const uint32 PageIndex = Child.GetCluster(ClusterDAG).PageIndex;

					// sanity check instanced pages >= group pages
					check(PageIndex >= Group.PageRangeKey.GetStartIndex());
					if (PageIndex >= Group.PageRangeKey.GetStartIndex() + Group.PageRangeKey.GetNumPagesOrRanges())
					{
						InstancedPages.AddUnique(PageIndex);
					}
				}
			}
			
			if (InstancedPages.Num() == 0)
			{
				// All instances are in the same pages as the group
				continue;
			}

			InstancedPages.Sort();

			// Consolidate instanced pages into consecutive ranges
			uint32 RangeStartIndex, RangeCount;
			int32 NextInstancedPage = 0;
			if (Group.PageRangeKey.IsEmpty())
			{
				// Group has only instances, start the range with the first instanced page index
				RangeStartIndex = InstancedPages[0];
				RangeCount = 1;
				++NextInstancedPage;
			}
			else
			{
				// Start with the group's own page range, in case any instanced pages are consecutive
				RangeStartIndex = Group.PageRangeKey.GetStartIndex();
				RangeCount = Group.PageRangeKey.GetNumPagesOrRanges();
			}

			bool bAnyStreamingPages = false;
			for (; NextInstancedPage < InstancedPages.Num(); ++NextInstancedPage, ++RangeCount)
			{
				const uint32 PageIndex = InstancedPages[NextInstancedPage];
				if (PageIndex != RangeStartIndex + RangeCount)
				{
					// We found a gap. Add the range we've accumulated so far and reset
					const bool bHasStreamingPages = RangeStartIndex + RangeCount > MaxRootPages;
					bAnyStreamingPages |= bHasStreamingPages;
					PageRanges.Emplace(RangeStartIndex, RangeCount, /* bMultiRange = */ false, bHasStreamingPages);
					RangeStartIndex = PageIndex;
					RangeCount = 0;
				}
			}
			const bool bHasStreamingPages = RangeStartIndex + RangeCount > MaxRootPages;
			bAnyStreamingPages |= bHasStreamingPages;
			PageRanges.Emplace(RangeStartIndex, RangeCount, /* bMultiRange = */ false, bHasStreamingPages);

			if (PageRanges.Num() == 1)
			{
				// We were able to consolidate all into a single range
				Group.PageRangeKey = PageRanges[0];
				continue;
			}

			// Add the group's multiple range list to a lookup and replace its lookup key with a multi-range key (range of ranges)
			// De-duplicate where possible (Note: PageRanges should be sorted)
			RangeStartIndex = PageRangeLookup.Num();
			RangeCount = PageRanges.Num();
			for (int32 i = 0; i < PageRangeLookup.Num(); ++i)
			{
				const int32 NumToCompare = FMath::Min<int32>(RangeCount, PageRangeLookup.Num() - i);
				if (CompareItems(&PageRangeLookup[i], PageRanges.GetData(), NumToCompare))
				{
					RangeStartIndex = i;
					break;
				}
			}

			const int32 NumExisting = PageRangeLookup.Num() - RangeStartIndex;
			const int32 NumToAdd = RangeCount - NumExisting;
			if (NumToAdd > 0)
			{
				PageRangeLookup.Append(MakeConstArrayView(&PageRanges[NumExisting], NumToAdd));
			}
			
			Group.PageRangeKey = FPageRangeKey(RangeStartIndex, RangeCount, /* bMultiRange = */ true, bAnyStreamingPages);
		}
	}

	// Calculate bounds for group parts
	for (FClusterGroupPart& Part : Parts)
	{
		check(Part.Clusters.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP);
		check(Part.PageIndex < (uint32)Pages.Num());

		FBounds3f Bounds;
		float MinLODError = MAX_flt;
		for (uint32 ClusterIndex : Part.Clusters)
		{
			const FCluster& Cluster = Clusters[ClusterIndex];
			const float LODError = (Cluster.GeneratingGroupIndex == MAX_uint32) ? 0.0f : Cluster.LODError;	// Handle trim

			Bounds += Cluster.Bounds;
			MinLODError = FMath::Min(MinLODError, LODError);
		}
		Part.Bounds = Bounds;
		Part.MinLODError = MinLODError;
	}
}

} // namespace Nanite
