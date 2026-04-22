// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/NaniteResources.h"

#include "NaniteDefinitions.h"

#define NANITE_FIXUP_FLAG_INSTALLED 1


namespace Nanite
{

class FFixupChunk
{
public:
	struct FHeader
	{
		uint16 Magic;
		uint16 NumGroupFixups;
		uint16 NumPartFixups;
		uint16 NumClusters;
		uint16 NumReconsiderPages;	// Pages that need to be reconsidered for fixup when this page is installed/uninstalled. The last pages of any groups in the page.
		uint16 Pad;
		uint32 NumParentFixups;
		uint32 NumHierarchyLocations;
		uint32 NumClusterIndices;
	} Header;

	struct FHierarchyLocation
	{
		uint32 ChildIndex_NodeIndex;

		FHierarchyLocation(uint32 NodeIndex, uint32 ChildIndex) :
			ChildIndex_NodeIndex(0)
		{
			SetChildIndex(ChildIndex);
			SetNodeIndex(NodeIndex);
		}

		uint32 GetChildIndex() const { return ChildIndex_NodeIndex & NANITE_MAX_BVH_NODE_FANOUT_MASK; }
		uint32 GetNodeIndex() const { return ChildIndex_NodeIndex >> NANITE_MAX_BVH_NODE_FANOUT_BITS; }

		void SetChildIndex(uint32 Index) { SetBits(ChildIndex_NodeIndex, Index, NANITE_MAX_BVH_NODE_FANOUT_BITS, 0); }
		void SetNodeIndex(uint32 Index) { SetBits(ChildIndex_NodeIndex, Index, NANITE_MAX_NODES_PER_PRIMITIVE_BITS, NANITE_MAX_BVH_NODE_FANOUT_BITS); }
	};

	// TODO: Consider further trimming structs and/or omitting some of the offsets where we don't need random access
	struct FPartFixup
	{
		uint16	PageIndex;
		uint8	StartClusterIndex;
		uint8	LeafCounter;

		uint32	FirstHierarchyLocation;
		uint32	NumHierarchyLocations;
	};

	struct FParentFixup
	{
		uint16	PageIndex;

		uint16	PartFixupPageIndex;
		uint16	PartFixupIndex;

		uint16	NumClusterIndices;
		uint16	FirstClusterIndex;
	};

	struct FGroupFixup
	{
		FPageRangeKey	PageDependencies;
		uint32			Flags;

		uint16			FirstPartFixup;
		uint16			NumPartFixups;

		uint16			FirstParentFixup;
		uint16			NumParentFixups;
	};

	static constexpr uint32 GetSize(uint32 NumGroupFixups, uint32 NumPartFixups, uint32 NumParentFixups, uint32 NumHierarchyLocations, uint32 NumReconsiderPages, uint32 NumClusterIndices)
	{
		return	sizeof(FHeader) +
			NumGroupFixups * sizeof(FGroupFixup) +
			NumPartFixups * sizeof(FPartFixup) +
			NumParentFixups * sizeof(FParentFixup) +
			NumHierarchyLocations * sizeof(FHierarchyLocation) +
			NumReconsiderPages * sizeof(uint16) +
			NumClusterIndices * sizeof(uint8);
	}

	FGroupFixup& GetGroupFixup(uint32 Index) const
	{
		check(Index < Header.NumGroupFixups);
		FGroupFixup* Ptr = (FGroupFixup*)((uint8*)&Header + GetSize(0, 0, 0, 0, 0, 0));
		return Ptr[Index];
	}

	FPartFixup& GetPartFixup(uint32 Index) const
	{
		check(Index < Header.NumPartFixups);
		FPartFixup* Ptr = (FPartFixup*)((uint8*)&Header + GetSize(Header.NumGroupFixups, 0, 0, 0, 0, 0));
		return Ptr[Index];
	}

	FParentFixup& GetParentFixup(uint32 Index) const
	{
		check(Index < Header.NumParentFixups);
		FParentFixup* Ptr = (FParentFixup*)((uint8*)&Header + GetSize(Header.NumGroupFixups, Header.NumPartFixups, 0, 0, 0, 0));
		return Ptr[Index];
	}

	FHierarchyLocation& GetHierarchyLocation(uint32 Index) const
	{
		check(Index < Header.NumHierarchyLocations);
		FHierarchyLocation* Ptr = (FHierarchyLocation*)((uint8*)&Header + GetSize(Header.NumGroupFixups, Header.NumPartFixups, Header.NumParentFixups, 0, 0, 0));
		return Ptr[Index];
	}

	uint16& GetReconsiderPageIndex(uint32 Index) const
	{
		check(Index < Header.NumReconsiderPages);
		uint16* Ptr = (uint16*)((uint8*)&Header + GetSize(Header.NumGroupFixups, Header.NumPartFixups, Header.NumParentFixups, Header.NumHierarchyLocations, 0, 0));
		return Ptr[Index];
	}


	uint8& GetClusterIndex(uint32 Index) const
	{
		check(Index < Header.NumClusterIndices);
		uint8* Ptr = (uint8*)&Header + GetSize(Header.NumGroupFixups, Header.NumPartFixups, Header.NumParentFixups, Header.NumHierarchyLocations, Header.NumReconsiderPages, 0);
		return Ptr[Index];
	}

	uint32 GetSize() const
	{
		return GetSize(Header.NumGroupFixups, Header.NumPartFixups, Header.NumParentFixups, Header.NumHierarchyLocations, Header.NumReconsiderPages, Header.NumClusterIndices);
	}
};

}