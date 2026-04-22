// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeHierarchy.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteEncodeShared.h"

#define BVH_BUILD_WRITE_GRAPHVIZ	0

namespace Nanite
{

struct FIntermediateNode
{
	uint32				PartIndex					= MAX_uint32;
	uint32				AssemblyRootIndex			= MAX_uint32;
	uint32				NewAssemblyInstanceIndex	= MAX_uint32;	// To be able to share subtrees this is only set when the instance changes.
	uint32				HierarchyNodeIndex			= MAX_uint32;
	
	uint32				MipLevel					= MAX_int32;
	
	FBounds3f			Bound;
	TArray< uint32 >	Children;

	bool IsLeaf() const { return PartIndex != MAX_uint32; }
};

struct FHierarchyNode
{
	FSphere3f	LODBounds[NANITE_MAX_BVH_NODE_FANOUT];
	FBounds3f	Bounds[NANITE_MAX_BVH_NODE_FANOUT];
	float		MinLODErrors[NANITE_MAX_BVH_NODE_FANOUT];
	float		MaxParentLODErrors[NANITE_MAX_BVH_NODE_FANOUT];
	uint32		ChildrenStartIndex[NANITE_MAX_BVH_NODE_FANOUT];
	uint32		NumChildren[NANITE_MAX_BVH_NODE_FANOUT];
	uint32		ClusterGroupPartIndex[NANITE_MAX_BVH_NODE_FANOUT];
	uint32		AssemblyTransformIndex[NANITE_MAX_BVH_NODE_FANOUT];
};

static void WriteDotGraph(const TArray<FHierarchyNode>& Nodes)
{
	auto IsEnabled = [](const FHierarchyNode& Node, uint32 ChildIndex) -> bool
		{
			return Node.NumChildren[ChildIndex] != 0u;
		};

	auto IsLeaf = [](const FHierarchyNode& Node, uint32 ChildIndex) -> bool
		{
			return Node.ClusterGroupPartIndex[ChildIndex] != MAX_uint32;
		};

	FGenericPlatformMisc::LowLevelOutputDebugString(TEXT("digraph {\n"));
	FGenericPlatformMisc::LowLevelOutputDebugString(TEXT("node[shape = record]\n"));

	TSet<uint32> ReferencedNodes;

	const uint32 NumNodes = Nodes.Num();
	for (uint32 NodeIndex = 0; NodeIndex < NumNodes; NodeIndex++)
	{
		const FHierarchyNode& Node = Nodes[NodeIndex];

		for (uint32 ChildIndex = 0; ChildIndex < NANITE_MAX_BVH_NODE_FANOUT; ChildIndex++)
		{
			if (IsEnabled(Node, ChildIndex) && !IsLeaf(Node, ChildIndex))
			{
				const uint32 ChildNodeIndex = Node.ChildrenStartIndex[ChildIndex];

				FGenericPlatformMisc::LowLevelOutputDebugStringf(TEXT("n%d->n%d\n"), NodeIndex, ChildNodeIndex);
				ReferencedNodes.Add(NodeIndex);
				ReferencedNodes.Add(ChildNodeIndex);
			}
		}
	}

	for (uint32 NodeIndex : ReferencedNodes)
	{
		const FHierarchyNode& Node = Nodes[NodeIndex];

		FString Str = FString::Printf(TEXT("n%d [label=\""), NodeIndex);
		for (uint32 ChildIndex = 0; ChildIndex < NANITE_MAX_BVH_NODE_FANOUT; ChildIndex++)
		{
			if (IsEnabled(Node, ChildIndex))
			{
				if (ChildIndex != 0)
					Str += TEXT("|");
				
				Str += FString::Printf(TEXT("{%.2f|<f%d>%.2f}"),
					Node.MaxParentLODErrors[ChildIndex],
					ChildIndex,
					Node.LODBounds[ChildIndex].W
				);
			}
		}
		Str += TEXT("\"]\n");
		FGenericPlatformMisc::LowLevelOutputDebugStringf(*Str);
	}


	FGenericPlatformMisc::LowLevelOutputDebugString(TEXT("\n}\n"));
}

static uint32 BuildHierarchyRecursive(
	TArray<FHierarchyNode>& HierarchyNodes,
	TArray<FIntermediateNode>& Nodes,
	const FClusterDAG& ClusterDAG,
	TArray<FClusterGroupPart>& Parts,
	uint32 AssemblyInstanceIndex,
	uint32 CurrentNodeIndex,
	bool& bOutIsInstanceDependent	//TODO: Temporary hack to make sure hierarchy nodes for root groups are not instanced, so their unique ParentErrors are respected.
)
{
	const FIntermediateNode& INode = Nodes[ CurrentNodeIndex ];
	check( INode.PartIndex == MAX_uint32 );
	check( !INode.IsLeaf() );

	uint32 HNodeIndex = HierarchyNodes.Num();
	HierarchyNodes.AddZeroed();

	bOutIsInstanceDependent = false;

	uint32 NumChildren = INode.Children.Num();
	check(NumChildren <= NANITE_MAX_BVH_NODE_FANOUT);
	for( uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++ )
	{
		uint32 ChildNodeIndex = INode.Children[ ChildIndex ];
		const FIntermediateNode* ChildNode = &Nodes[ ChildNodeIndex ];
		const uint32 ChildNewAssemblyInstanceIndex = ChildNode->NewAssemblyInstanceIndex;
		
		uint32 ChildAssemblyInstanceIndex = AssemblyInstanceIndex;
		if( ChildNode->AssemblyRootIndex != MAX_uint32 )
		{
			// Forward assembly ref to real node
			ChildNodeIndex = ChildNode->AssemblyRootIndex;
			ChildNode = &Nodes[ ChildNodeIndex ];
			check( ChildNode->AssemblyRootIndex == MAX_uint32 );

			ChildAssemblyInstanceIndex = ChildNewAssemblyInstanceIndex;
		}

		if( ChildNode->IsLeaf() )
		{
			// Cluster Group
			FClusterGroupPart& Part = Parts[ChildNode->PartIndex];
			const FClusterGroup& Group = ClusterDAG.Groups[Part.GroupIndex];

			FSphere3f LODBounds		= Group.LODBounds;
			float ParentLODError	= Group.ParentLODError;
			if( Group.bRoot && ChildAssemblyInstanceIndex != MAX_uint32 )
			{
				LODBounds		= ClusterDAG.AssemblyInstanceData[ ChildAssemblyInstanceIndex ].LODBounds;
				ParentLODError	= ClusterDAG.AssemblyInstanceData[ ChildAssemblyInstanceIndex ].ParentLODError;

				const FMatrix44f& Transform = ClusterDAG.AssemblyInstanceData[ ChildAssemblyInstanceIndex ].Transform;
				const float MaxScale = Transform.GetScaleVector().GetMax();

				// Invert the transform to put back in assembly part local space.
				LODBounds.Center = Transform.InverseTransformPosition( LODBounds.Center );
				LODBounds.W		/= MaxScale;
				ParentLODError	/= MaxScale;

				check( ParentLODError >= 0.0f );
				bOutIsInstanceDependent = true;
			}

			FHierarchyNode& HNode = HierarchyNodes[HNodeIndex];
			HNode.Bounds[ChildIndex] = Part.Bounds;
			HNode.LODBounds[ChildIndex] = LODBounds;
			HNode.MinLODErrors[ChildIndex] = Part.MinLODError;
			HNode.MaxParentLODErrors[ChildIndex] = ParentLODError;
			HNode.ChildrenStartIndex[ChildIndex] = 0xFFFFFFFFu;
			HNode.NumChildren[ChildIndex] = Part.Clusters.Num();
			HNode.ClusterGroupPartIndex[ChildIndex] = ChildNode->PartIndex;
			HNode.AssemblyTransformIndex[ChildIndex] = ChildNewAssemblyInstanceIndex;

			check(HNode.NumChildren[ChildIndex] <= NANITE_MAX_CLUSTERS_PER_GROUP);
			Part.HierarchyNodeRefs.Add(FHierarchyNodeRef{ HNodeIndex, ChildIndex });
		}
		else
		{
			// Hierarchy node
			uint32 ChildHierarchyNodeIndex = ChildNode->HierarchyNodeIndex;
			if (ChildHierarchyNodeIndex == MAX_uint32)
			{
				// Only recurse when it isn't cached
				bool bChildIsInstanceDependent;
				ChildHierarchyNodeIndex = BuildHierarchyRecursive(HierarchyNodes, Nodes, ClusterDAG, Parts, ChildAssemblyInstanceIndex, ChildNodeIndex, bChildIsInstanceDependent);
				
				if (!bChildIsInstanceDependent)
				{
					// TODO: Caching the subhierarchy currently gives LOD artifacts at the transition to the mip tail because MaxParentLODError is unique per instance.
					//		 Fix this in a follow-up commit.
					Nodes[ChildNodeIndex].HierarchyNodeIndex = ChildHierarchyNodeIndex;	// Deliberately don't reference ChildNode past recursive call
				}

				bOutIsInstanceDependent |= bChildIsInstanceDependent;
			}
			
			const FHierarchyNode& ChildHNode = HierarchyNodes[ChildHierarchyNodeIndex];

			FBounds3f Bounds;
			TArray< FSphere3f, TInlineAllocator<NANITE_MAX_BVH_NODE_FANOUT> > LODBoundSpheres;
			float MinLODError = MAX_flt;
			float MaxParentLODError = 0.0f;
			for (uint32 GrandChildIndex = 0; GrandChildIndex < NANITE_MAX_BVH_NODE_FANOUT && ChildHNode.NumChildren[GrandChildIndex] != 0; GrandChildIndex++)
			{
				const uint32 GrandChildAssemblyTransformIndex = ChildHNode.AssemblyTransformIndex[GrandChildIndex];

				float GrandChildMinLODError = ChildHNode.MinLODErrors[GrandChildIndex];
				float GrandChildMaxParentLODError = ChildHNode.MaxParentLODErrors[GrandChildIndex];

				if (GrandChildAssemblyTransformIndex != MAX_uint32)
				{
					const FMatrix44f& Transform = ClusterDAG.AssemblyInstanceData[GrandChildAssemblyTransformIndex].Transform;
					const float MaxScale = Transform.GetScaleVector().GetMax();

					Bounds += ChildHNode.Bounds[GrandChildIndex].TransformBy( Transform );

					FSphere3f LODBounds = ChildHNode.LODBounds[GrandChildIndex];
					LODBounds.Center = Transform.TransformPosition(LODBounds.Center);
					LODBounds.W *= MaxScale;
					GrandChildMinLODError *= MaxScale;
					GrandChildMaxParentLODError *= MaxScale;
					LODBoundSpheres.Add(LODBounds);
				}
				else
				{
					Bounds += ChildHNode.Bounds[GrandChildIndex];
					LODBoundSpheres.Add(ChildHNode.LODBounds[GrandChildIndex]);
				}
				
				MinLODError = FMath::Min(MinLODError, GrandChildMinLODError);
				MaxParentLODError = FMath::Max(MaxParentLODError, GrandChildMaxParentLODError);
			}

			FSphere3f LODBounds = FSphere3f(LODBoundSpheres.GetData(), LODBoundSpheres.Num());

			FHierarchyNode& HNode = HierarchyNodes[HNodeIndex];
			HNode.Bounds[ChildIndex] = Bounds;
			HNode.LODBounds[ChildIndex] = LODBounds;
			HNode.MinLODErrors[ChildIndex] = MinLODError;
			HNode.MaxParentLODErrors[ChildIndex] = MaxParentLODError;
			HNode.ChildrenStartIndex[ChildIndex] = ChildHierarchyNodeIndex;
			HNode.NumChildren[ChildIndex] = NANITE_MAX_CLUSTERS_PER_GROUP;
			HNode.ClusterGroupPartIndex[ChildIndex] = MAX_uint32;
			HNode.AssemblyTransformIndex[ChildIndex] = ChildNewAssemblyInstanceIndex;
		}
	}

	return HNodeIndex;
}

static float BVH_Cost(const TArray<FIntermediateNode>& Nodes, TArrayView<uint32> NodeIndices)
{
	FBounds3f Bound;
	for (uint32 NodeIndex : NodeIndices)
	{
		Bound += Nodes[NodeIndex].Bound;
	}
	return Bound.GetSurfaceArea();
}

static void BVH_SortNodes(const TArray<FIntermediateNode>& Nodes, TArrayView<uint32> NodeIndices, const TArray<uint32>& ChildSizes)
{
	// Perform NANITE_MAX_BVH_NODE_FANOUT_BITS binary splits
	for (uint32 Level = 0; Level < NANITE_MAX_BVH_NODE_FANOUT_BITS; Level++)
	{
		const uint32 NumBuckets = 1 << Level;
		const uint32 NumChildrenPerBucket = NANITE_MAX_BVH_NODE_FANOUT >> Level;
		const uint32 NumChildrenPerBucketHalf = NumChildrenPerBucket >> 1;

		uint32 BucketStartIndex = 0;
		for (uint32 BucketIndex = 0; BucketIndex < NumBuckets; BucketIndex++)
		{
			const uint32 FirstChild = NumChildrenPerBucket * BucketIndex;
			
			uint32 Sizes[2] = {};
			for (uint32 i = 0; i < NumChildrenPerBucketHalf; i++)
			{
				Sizes[0] += ChildSizes[FirstChild + i];
				Sizes[1] += ChildSizes[FirstChild + i + NumChildrenPerBucketHalf];
			}
			TArrayView<uint32> NodeIndices01 = NodeIndices.Slice(BucketStartIndex, Sizes[0] + Sizes[1]);
			TArrayView<uint32> NodeIndices0 = NodeIndices.Slice(BucketStartIndex, Sizes[0]);
			TArrayView<uint32> NodeIndices1 = NodeIndices.Slice(BucketStartIndex + Sizes[0], Sizes[1]);

			BucketStartIndex += Sizes[0] + Sizes[1];

			auto SortByAxis = [&](uint32 AxisIndex)
			{
				if (AxisIndex == 0)
					NodeIndices01.Sort([&Nodes](uint32 A, uint32 B) { return Nodes[A].Bound.GetCenter().X < Nodes[B].Bound.GetCenter().X; });
				else if (AxisIndex == 1)
					NodeIndices01.Sort([&Nodes](uint32 A, uint32 B) { return Nodes[A].Bound.GetCenter().Y < Nodes[B].Bound.GetCenter().Y; });
				else if (AxisIndex == 2)
					NodeIndices01.Sort([&Nodes](uint32 A, uint32 B) { return Nodes[A].Bound.GetCenter().Z < Nodes[B].Bound.GetCenter().Z; });
				else
					check(false);
			};

			float BestCost = MAX_flt;
			uint32 BestAxisIndex = 0;

			// Try sorting along different axes and pick the best one
			const uint32 NumAxes = 3;
			for (uint32 AxisIndex = 0; AxisIndex < NumAxes; AxisIndex++)
			{
				SortByAxis(AxisIndex);

				float Cost = BVH_Cost(Nodes, NodeIndices0) + BVH_Cost(Nodes, NodeIndices1);
				if (Cost < BestCost)
				{
					BestCost = Cost;
					BestAxisIndex = AxisIndex;
				}
			}

			// Resort if we the best one wasn't the last one
			if (BestAxisIndex != NumAxes - 1)
			{
				SortByAxis(BestAxisIndex);
			}
		}
	}
}

// Build hierarchy using a top-down splitting approach.
// WIP:	So far it just focuses on minimizing worst-case tree depth/latency.
//		It does this by building a complete tree with at most one partially filled level.
//		At most one node is partially filled.
//TODO:	Experiment with sweeping, even if it results in more total nodes and/or makes some paths slightly longer.
static uint32 BuildHierarchyTopDown(TArray<FIntermediateNode>& Nodes, TArrayView<uint32> NodeIndices, bool bSort)
{
	const uint32 N = NodeIndices.Num();
	if (N == 1)
	{
		return NodeIndices[0];
	} 
	
	const uint32 NewRootIndex = Nodes.Num();
	Nodes.AddDefaulted();

	if (N <= NANITE_MAX_BVH_NODE_FANOUT)
	{
		Nodes[NewRootIndex].Children = NodeIndices;
		return NewRootIndex;
	}

	// Where does the last (incomplete) level start
	uint32 TopSize = NANITE_MAX_BVH_NODE_FANOUT;
	while (TopSize * NANITE_MAX_BVH_NODE_FANOUT <= N)
	{
		TopSize *= NANITE_MAX_BVH_NODE_FANOUT;
	}
	
	const uint32 LargeChildSize = TopSize;
	const uint32 SmallChildSize = TopSize / NANITE_MAX_BVH_NODE_FANOUT;
	const uint32 MaxExcessPerChild = LargeChildSize - SmallChildSize;

	TArray<uint32> ChildSizes;
	ChildSizes.SetNum(NANITE_MAX_BVH_NODE_FANOUT);
	
	uint32 Excess = N - TopSize;
	for (int32 i = NANITE_MAX_BVH_NODE_FANOUT-1; i >= 0; i--)
	{
		const uint32 ChildExcess = FMath::Min(Excess, MaxExcessPerChild);
		ChildSizes[i] = SmallChildSize + ChildExcess;
		Excess -= ChildExcess;
	}
	check(Excess == 0);

	if (bSort)
	{
		BVH_SortNodes(Nodes, NodeIndices, ChildSizes);
	}
	
	uint32 Offset = 0;
	for (uint32 i = 0; i < NANITE_MAX_BVH_NODE_FANOUT; i++)
	{
		uint32 ChildSize = ChildSizes[i];
		uint32 NodeIndex = BuildHierarchyTopDown(Nodes, NodeIndices.Slice(Offset, ChildSize), bSort);	// Needs to be separated from next statement with sequence point to order access to Nodes array.
		Nodes[NewRootIndex].Children.Add(NodeIndex);
		Offset += ChildSize;
	}

	return NewRootIndex;
}

static uint32 BuildHierarchyLODRoots(TArray<FIntermediateNode>& Nodes, TArrayView<uint32> NodeIndices, uint32 ChainLevels)
{
	const uint32 N = NodeIndices.Num();
	if (N == 1)
	{
		return NodeIndices[0];
	}

	if (N <= NANITE_MAX_BVH_NODE_FANOUT)
	{
		const uint32 NewRootIndex = Nodes.Num();
		Nodes.AddDefaulted();
		Nodes[NewRootIndex].Children = NodeIndices;
		return NewRootIndex;
	}
	else
	{
		if (ChainLevels == 0)
		{
			return BuildHierarchyTopDown(Nodes, NodeIndices, false);
		}
		else
		{
			const uint32 NewRootIndex = Nodes.Num();
			Nodes.AddDefaulted();

			Nodes[NewRootIndex].Children = NodeIndices.Slice(NodeIndices.Num() - 3, 3);	// Last 3 LOD levels directly
			uint32 ChildNode = BuildHierarchyLODRoots(Nodes, NodeIndices.Slice(0, NodeIndices.Num() - 3), ChainLevels - 1);
			Nodes[NewRootIndex].Children.Add(ChildNode);
			return NewRootIndex;
		}
	}
}

static void CalculatePageHierarchyPartDepths(TArray<FPage>& Pages, const TArray<FClusterGroupPart>& Parts, const TArray<FHierarchyNode>& HierarchyNodes, uint32 NodeIndex, uint32 Depth)
{
	const FHierarchyNode& Node = HierarchyNodes[NodeIndex];

	for (uint32 i = 0; i < NANITE_MAX_BVH_NODE_FANOUT; i++)
	{
		if (Node.NumChildren[i] > 0)
		{
			if (Node.ClusterGroupPartIndex[i] != MAX_uint32)
			{
				// Leaf node
				const FClusterGroupPart& Part = Parts[Node.ClusterGroupPartIndex[i]];
				uint32& MaxDepth = Pages[Part.PageIndex].MaxHierarchyPartDepth;
				MaxDepth = FMath::Max(MaxDepth, Depth);
				check(MaxDepth <= NANITE_MAX_CLUSTER_HIERARCHY_DEPTH);
			}
			else
			{
				CalculatePageHierarchyPartDepths(Pages, Parts, HierarchyNodes, Node.ChildrenStartIndex[i], Depth + 1);
			}
		}
	}
}

// Update worst-case hierarchy depths for pages.
// These are used to determine how many node culling passes are needed at runtime.
void CalculateFinalPageHierarchyDepth(const FResources& Resources, TArray<FPage>& Pages)
{
	// Calculate final MaxHierarchyDepth based hierarchy depth of the parts.

	// Make sure all pages are reachable from at least one of its dependencies,
	// otherwise we can potentially get into a situation where streaming can't progress any further.
	for (uint32 PageIndex = 0; PageIndex < (uint32)Pages.Num(); PageIndex++)
	{
		Pages[PageIndex].MaxHierarchyDepth = FMath::Max(Pages[PageIndex].MaxHierarchyDepth, Pages[PageIndex].MaxHierarchyPartDepth);

		const FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];

		// Determine max hierarchy depth of dependencies
		uint32 DependencyMaxDepth = 0;
		uint32 DependencyMaxDepthIndex = MAX_uint32;
		for (uint32 i = 0; i < PageStreamingState.DependenciesNum; i++)
		{
			const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + i];
			check(DependencyPageIndex < PageIndex);

			const uint32 DependencyPageDepth = Pages[DependencyPageIndex].MaxHierarchyDepth;
			if (DependencyPageDepth >= DependencyMaxDepth)	// >= so DependencyMaxDepthIndex also gets initialized for DependencyPageDepth=0
			{
				DependencyMaxDepth = DependencyPageDepth;
				DependencyMaxDepthIndex = i;
			}
		}

		// If page parts can't be reached, increase the depth of the dependency that already has the highest depth	
		if (DependencyMaxDepthIndex != MAX_uint32 && DependencyMaxDepth < Pages[PageIndex].MaxHierarchyPartDepth)
		{
			const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + DependencyMaxDepthIndex];
			Pages[DependencyPageIndex].MaxHierarchyDepth = Pages[PageIndex].MaxHierarchyPartDepth;
		}
	}
}

// TODO: Do a cleaner and shared implementation?
static FFloat16 Float32ToFloat16Floor(float Value)
{
	FFloat16 ValueF16 = FFloat16(Value).Encoded;
	while(ValueF16.GetFloat() > Value && ValueF16.Encoded != 0xFFFFu) // This should only loop once or maybe twice under normal circumstances
	{
		if (ValueF16.Encoded == 0u)
			ValueF16.Encoded = 0x8000u;
		else
			ValueF16.Encoded += (ValueF16.Encoded & 0x8000u) ? 1 : -1;
	}
	return ValueF16;
}

static void PackHierarchyNode(FPackedHierarchyNode& OutNode, const FHierarchyNode& InNode, const TArray<FClusterGroup>& Groups, const TArray<FClusterGroupPart>& GroupParts, const uint32 NumResourceRootPages)
{
	static_assert(NANITE_MAX_RESOURCE_PAGES_BITS + NANITE_MAX_CLUSTERS_PER_GROUP_BITS + NANITE_MAX_GROUP_PARTS_BITS <= 32, "");
	for (uint32 i = 0; i < NANITE_MAX_BVH_NODE_FANOUT; i++)
	{
		OutNode.LODBounds[i] = FVector4f(InNode.LODBounds[i].Center, InNode.LODBounds[i].W);

		const FBounds3f& Bounds = InNode.Bounds[i];
		OutNode.Misc0[i].BoxBoundsCenter = Bounds.GetCenter();
		OutNode.Misc1[i].BoxBoundsExtent = Bounds.GetExtent();

		check(InNode.NumChildren[i] <= NANITE_MAX_CLUSTERS_PER_GROUP);

		OutNode.Misc0[i].MinLODError_MaxParentLODError	= FFloat16( InNode.MaxParentLODErrors[i] ).Encoded | ( Float32ToFloat16Floor(InNode.MinLODErrors[i]).Encoded << 16 );
		OutNode.Misc1[i].ChildStartReference			= InNode.ChildrenStartIndex[i];

		uint32 ResourcePageRangeKey = NANITE_PAGE_RANGE_KEY_EMPTY_RANGE;
		uint32 GroupPartSize = 0;
		if( InNode.NumChildren[ i ] > 0 )
		{
			if( InNode.ClusterGroupPartIndex[ i ] != MAX_uint32 )
			{
				// Leaf node
				const FClusterGroup& Group = Groups[ GroupParts[ InNode.ClusterGroupPartIndex[ i ] ].GroupIndex ];
				ResourcePageRangeKey = Group.PageRangeKey.Value;
				GroupPartSize = InNode.NumChildren[ i ];
			}
			else
			{
				// Hierarchy node. No resource pages or group size.
				ResourcePageRangeKey = 0xFFFFFFFFu;
			}
		}
		OutNode.Misc2[ i ].ResourcePageRangeKey = ResourcePageRangeKey;
		OutNode.Misc2[ i ].GroupPartSize_AssemblyPartIndex =
			(InNode.AssemblyTransformIndex[i] & NANITE_HIERARCHY_MAX_ASSEMBLY_TRANSFORMS) |
			(GroupPartSize << NANITE_HIERARCHY_ASSEMBLY_TRANSFORM_INDEX_BITS);
	}
}

static uint32 BuildHierarchy(
	TArray<FIntermediateNode>& Nodes,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts,
	const TArray<uint32>& AssemblyRoots,
	const TArray<uint32>& PartIndices,
	bool bAssemblyPart)
{
	const TArray<FClusterGroup>& Groups = ClusterDAG.Groups;
	const uint32 NumGroupParts			= (uint32)PartIndices.Num();
	const uint32 NumAssemblyParts		= (uint32)ClusterDAG.AssemblyPartData.Num();
	const uint32 NumAssemblyInstances	= (uint32)ClusterDAG.AssemblyInstanceData.Num();
	check(AssemblyRoots.Num() == ClusterDAG.AssemblyPartData.Num());

	int32 MaxMipLevel = 0;
	for(const uint32 PartIndex : PartIndices)
	{
		const FClusterGroupPart& Part = Parts[PartIndex];
		const FClusterGroup& Group = Groups[Part.GroupIndex];
		MaxMipLevel = FMath::Max(MaxMipLevel, Group.MipLevel);
	}

	MaxMipLevel++;

	const uint32 AssemblyMipLevel = 0;
	const uint32 FirstNodeIndex = (uint32)Nodes.Num();
	
	// Build leaf nodes for each LOD level of the mesh
	TArray<TArray<uint32>> NodesByMip;
	NodesByMip.SetNum(MaxMipLevel + 1);
	{
		Nodes.AddDefaulted(NumGroupParts);
		for (uint32 i = 0; i < NumGroupParts; i++)
		{
			const uint32 PartIndex = PartIndices[i];
			const FClusterGroupPart& Part = Parts[PartIndex];
			const FClusterGroup& Group = Groups[Part.GroupIndex];
		
			const uint32 NodeIndex = FirstNodeIndex + i;
			FIntermediateNode& Node = Nodes[NodeIndex];
			Node.Bound = Part.Bounds;
			Node.PartIndex = PartIndex;
			Node.MipLevel = Group.MipLevel + 1;
			NodesByMip[Group.MipLevel + 1].Add(NodeIndex);
		}
	}

	if(!bAssemblyPart)
	{
		Nodes.Reserve(Nodes.Num() + NumAssemblyInstances);

		for (uint32 i = 0; i < NumAssemblyParts; i++)
		{
			const FAssemblyPartData& PartData = ClusterDAG.AssemblyPartData[i];
			const uint32 AssemblyRootIndex = AssemblyRoots[i];

			if (AssemblyRootIndex == MAX_uint32)
				continue;

			for (uint32 j = 0; j < PartData.NumInstances; j++)
			{
				const uint32 InstanceIndex = PartData.FirstInstance + j;
				
				NodesByMip[AssemblyMipLevel].Add(Nodes.Num());

				FIntermediateNode& Node = Nodes.AddDefaulted_GetRef();
				Node.Bound = Nodes[AssemblyRootIndex].Bound.TransformBy( ClusterDAG.AssemblyInstanceData[InstanceIndex].Transform );
				Node.AssemblyRootIndex = AssemblyRootIndex;
				Node.NewAssemblyInstanceIndex = InstanceIndex;
				Node.MipLevel = AssemblyMipLevel;	
			}
		}
	}
	
	const uint32 NumNewNodes = (uint32)Nodes.Num() - FirstNodeIndex;

	uint32 RootIndex = MAX_uint32;
	if (NumNewNodes == 0)
	{
		if (!bAssemblyPart)
		{
			RootIndex = (uint32)Nodes.Num();
			Nodes.AddDefaulted();
		}
	}
	else if (NumNewNodes == 1)
	{
		if (!bAssemblyPart)
		{
			FIntermediateNode& Node = Nodes.AddDefaulted_GetRef();
			Node.Children.Add( uint32 ( Nodes.Num() - 2 ) );
			Node.Bound = Nodes[0].Bound;
		}

		RootIndex = uint32( Nodes.Num() - 1 );
	}
	else
	{
		// Build hierarchy:
		// Nanite meshes contain cluster data for many levels of detail. Clusters from different levels
		// of detail can vary wildly in size, which can already be challenge for building a good hierarchy. 
		// Apart from the visibility bounds, the hierarchy also tracks conservative LOD error metrics for the child nodes.
		// The runtime traversal descends into children as long as they are visible and the conservative LOD error is not
		// more detailed than what we are looking for. We have to be very careful when mixing clusters from different LODs
		// as less detailed clusters can easily end up bloating both bounds and error metrics.

		// We have experimented with a bunch of mixed LOD approached, but currently, it seems, building separate hierarchies
		// for each LOD level and then building a hierarchy of those hierarchies gives the best and most predictable results.

		// TODO: The roots of these hierarchies all share the same visibility and LOD bounds, or at least close enough that we could
		//       make a shared conservative bound without losing much. This makes a lot of the work around the root node fairly
		//       redundant. Perhaps we should consider evaluating a shared root during instance cull instead and enable/disable
		//       the per-level hierarchies based on 1D range tests for LOD error.

		TArray<uint32> LevelRoots;
		for (int32 MipLevel = 0; MipLevel <= MaxMipLevel; MipLevel++)
		{
			if (NodesByMip[MipLevel].Num() > 0)
			{
				// Build a hierarchy for the mip level
				uint32 NodeIndex = BuildHierarchyTopDown(Nodes, NodesByMip[MipLevel], true);

				if (Nodes[NodeIndex].IsLeaf() || Nodes[NodeIndex].Children.Num() == NANITE_MAX_BVH_NODE_FANOUT)
				{
					// Leaf or filled node. Just add it.
					LevelRoots.Add(NodeIndex);
				}
				else
				{
					// Incomplete node. Discard the node and add the children as roots instead.
					LevelRoots.Append(Nodes[NodeIndex].Children);
				}
			}
		}
		// Build top hierarchy. A hierarchy of MIP hierarchies.
		RootIndex = BuildHierarchyLODRoots(Nodes, LevelRoots, 1);
	}

	return RootIndex;
}


void BuildHierarchies(
	FResources& Resources,
	const FClusterDAG& ClusterDAG,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts,
	uint32 NumMeshes)
{
	TArray<TArray<uint32>> RootMeshGroupParts;
	RootMeshGroupParts.SetNum(NumMeshes);

	const uint32 NumAssemblyParts = (uint32)ClusterDAG.AssemblyPartData.Num();

	TArray<TArray<uint32>> AssemblyGroupParts;
	AssemblyGroupParts.SetNum(NumAssemblyParts);

	TArray<uint32> AssemblyRoots;
	AssemblyRoots.SetNum(NumAssemblyParts);

	// Assign group part instances to their assembly part or a mesh root
	const uint32 NumTotalGroupParts = (uint32)Parts.Num();
	for (uint32 GroupPartIndex = 0; GroupPartIndex < NumTotalGroupParts; GroupPartIndex++)
	{
		const FClusterGroup& Group = ClusterDAG.Groups[Parts[GroupPartIndex].GroupIndex];

		if (Group.AssemblyPartIndex != MAX_uint32)
		{
			AssemblyGroupParts[Group.AssemblyPartIndex].Add(GroupPartIndex);
		}
		else
		{
			RootMeshGroupParts[Group.MeshIndex].Add(GroupPartIndex);
		}
	}

	TArray<FIntermediateNode> Nodes;
	for (uint32 AssemblyPartIndex = 0; AssemblyPartIndex < NumAssemblyParts; AssemblyPartIndex++)
	{
		const TArray<uint32>& GroupParts = AssemblyGroupParts[AssemblyPartIndex];

		uint32 RootIndex = MAX_uint32;
		if (GroupParts.Num() > 0)
		{
			RootIndex = BuildHierarchy(Nodes, ClusterDAG, Parts, AssemblyRoots, GroupParts, true);
		}

		AssemblyRoots[AssemblyPartIndex] = RootIndex;
	}

	for (uint32 MeshIndex = 0; MeshIndex < NumMeshes; MeshIndex++)
	{
		const uint32 RootIndex = BuildHierarchy(Nodes, ClusterDAG, Parts, AssemblyRoots, RootMeshGroupParts[MeshIndex], false);

		TArray<FHierarchyNode> HierarchyNodes;
		bool bIsInstanceDependent;
		BuildHierarchyRecursive(HierarchyNodes, Nodes, ClusterDAG, Parts, MAX_uint32, RootIndex, bIsInstanceDependent);

		CalculatePageHierarchyPartDepths(Pages, Parts, HierarchyNodes, 0, 0);

	#if BVH_BUILD_WRITE_GRAPHVIZ
		WriteDotGraph(HierarchyNodes);
	#endif

		// Convert hierarchy to packed format
		const uint32 NumHierarchyNodes = HierarchyNodes.Num();
		const uint32 PackedBaseIndex = Resources.HierarchyNodes.Num();
		Resources.HierarchyRootOffsets.Add(PackedBaseIndex);
		Resources.HierarchyNodes.AddDefaulted(NumHierarchyNodes);
		for (uint32 i = 0; i < NumHierarchyNodes; i++)
		{
			PackHierarchyNode(Resources.HierarchyNodes[PackedBaseIndex + i], HierarchyNodes[i], ClusterDAG.Groups, Parts, Resources.NumRootPages);
		}
	}
}

} // namespace Nanite
