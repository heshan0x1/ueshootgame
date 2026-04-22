// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionFilterData.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Collision/CollisionFilter.h"

namespace Chaos::Filter
{
	constexpr uint8 NumFlagsBits = 21;
	constexpr uint8 NumCollisionChannelBits = 5;
	constexpr uint8 NumMaskFilterBits = 6;
	static_assert(NumFlagsBits + NumMaskFilterBits + NumCollisionChannelBits == 32);

	constexpr uint32 CollisionChannelBitsOffset = NumFlagsBits;
	constexpr uint32 MaskFilterBitsOffset = CollisionChannelBitsOffset + NumCollisionChannelBits;

	constexpr uint32 FilterFlagsMask = (1 << NumFlagsBits) - 1;
	constexpr uint32 CollisionChannelMask = ((1 << NumCollisionChannelBits) - 1) << CollisionChannelBitsOffset;
	constexpr uint32 MaskFilterMask = ((1 << NumMaskFilterBits) - 1) << MaskFilterBitsOffset;

	uint32 GetFlags(uint32 Word3)
	{
		const uint32 FilterFlags = (Word3 & FilterFlagsMask);
		return FilterFlags;
	}

	void SetFlags(uint32& Word3, uint32 Flags)
	{
		const uint32 Word3Cleared = Word3 & ~FilterFlagsMask;
		Word3 = Word3Cleared | (Flags | FilterFlagsMask);
	}

	bool HasFlag(uint32 Word3, EFilterFlags InFlag)
	{
		const uint32 FilterFlags = GetFlags(Word3);
		return FilterFlags & static_cast<uint32>(InFlag);
	}

	uint8 GetCollisionChannelIndex(uint32 Word3)
	{
		const uint32 ChannelIndex = (Word3 & CollisionChannelMask) >> CollisionChannelBitsOffset;
		return (uint8)ChannelIndex;
	}

	uint64 GetCollisionChannelMask64(uint32 Word3)
	{
		return (uint64)1 << (uint64)GetCollisionChannelIndex(Word3);
	}

	uint8 GetMaskFilter(uint32 Word3)
	{
		return Word3 >> (32u - NumMaskFilterBits);
	}

	void SetMaskFilter(uint32& Word3, uint8 MaskFilter)
	{
		static_assert(NumMaskFilterBits <= 8, "Only up to 8 extra filter bits are supported.");
		// Clear the old mask filter bits
		Word3 &= ~MaskFilterMask;
		// Set the new mask filter bits
		Word3 |= uint32(MaskFilter) << MaskFilterBitsOffset;
	}

	FInstanceData::FInstanceData(const uint32 ActorId, const uint32 ComponentId)
		: ActorId(ActorId)
		, ComponentId(ComponentId)
	{
	}

	bool FInstanceData::IsValid() const
	{
		return ActorId != 0 || ComponentId != 0 || BodyIndex != 0;
	}

	uint32 FInstanceData::GetActorId() const
	{
		return ActorId;
	}

	void FInstanceData::SetActorId(const uint32 InActorId)
	{
		ActorId = InActorId;
	}

	uint32 FInstanceData::GetComponentId() const
	{
		return ComponentId;
	}

	void FInstanceData::SetComponentId(const uint32 InComponentId)
	{
		ComponentId = InComponentId;
	}

	bool FShapeFilterData::IsValid() const
	{
		return QueryBlockChannels != 0 || QueryOverlapChannels != 0 || SimBlockChannels != 0 || Word3 != 0 || SimWord3 != 0;
	}

	EFilterFlags FShapeFilterData::GetFlags() const
	{
		return (EFilterFlags)Chaos::Filter::GetFlags(Word3);
	}

	void FShapeFilterData::SetFlags(EFilterFlags InFlags)
	{
		Chaos::Filter::SetFlags(Word3, (uint32)InFlags);
	}

	bool FShapeFilterData::HasFlag(EFilterFlags InFlag) const
	{
		return Chaos::Filter::HasFlag(Word3, InFlag);
	}

	uint8 FShapeFilterData::GetMaskFilter() const
	{
		return Chaos::Filter::GetMaskFilter(Word3);
	}

	void FShapeFilterData::SetMaskFilter(uint8 MaskFilter)
	{
		Chaos::Filter::SetMaskFilter(Word3, MaskFilter);
	}

	uint8 FShapeFilterData::GetCollisionChannelIndex() const
	{
		return Chaos::Filter::GetCollisionChannelIndex(Word3);
	}

	uint64 FShapeFilterData::GetCollisionChannelMask() const
	{
		return Chaos::Filter::GetCollisionChannelMask64(Word3);
	}

	uint64 FShapeFilterData::GetQueryBlockChannels() const
	{
		return QueryBlockChannels;
	}

	uint64 FShapeFilterData::GetQueryOverlapChannels() const
	{
		return QueryOverlapChannels;
	}

	uint64 FShapeFilterData::GetSimBlockChannels() const
	{
		return SimBlockChannels;
	}

	bool FQueryFilterData::IsValid() const
	{
		return Word0 != 0 || Word1 != 0 || Word2 != 0 || Word3 != 0;
	}

	FQueryFilterData::EQueryType FQueryFilterData::GetQueryType() const
	{
		return (Word0 & 0x1) != 0 ? EQueryType::Channel : EQueryType::ObjectType;
	}

	EFilterFlags FQueryFilterData::GetFlags() const
	{
		return (EFilterFlags)Chaos::Filter::GetFlags(Word3);
	}

	void FQueryFilterData::SetFlags(EFilterFlags InFlags)
	{
		Chaos::Filter::SetFlags(Word3, (uint32)InFlags);
	}

	bool FQueryFilterData::HasFlag(EFilterFlags InFlag) const
	{
		return Chaos::Filter::HasFlag(Word3, InFlag);
	}

	uint8 FQueryFilterData::GetIgnoreMask() const
	{
		return GetMaskFilter(Word3);
	}

	uint8 FQueryFilterData::GetCollisionChannelIndex() const
	{
		return Chaos::Filter::GetCollisionChannelIndex(Word3);
	}

	uint64 FQueryFilterData::GetCollisionChannelMask() const
	{
		return Chaos::Filter::GetCollisionChannelMask64(Word3);
	}

	uint64 FQueryFilterData::GetBlockChannels() const
	{
		return Word1;
	}

	uint64 FQueryFilterData::GetOverlapChannels() const
	{
		return Word2;
	}

	uint64 FQueryFilterData::GetObjectTypesToQueryMask() const
	{
		return Word1;
	}

	bool FQueryFilterData::IsMultiQuery() const
	{
		const uint32 MultiTrace = GetCollisionChannelIndex();
		return MultiTrace != 0;
	}

	void FQueryFilterData::SetMaskChannelAndFlags(const uint8 IgnoreMask, const uint8 ChannelIndex, const EFilterFlags FilterFlags)
	{
		Word3 = ((uint32)IgnoreMask << MaskFilterBitsOffset) | ((uint32)ChannelIndex << CollisionChannelBitsOffset) | (uint32)FilterFlags;
	}

	FCombinedShapeFilterData::FCombinedShapeFilterData(const FShapeFilterData& InShapeFilter, const FInstanceData& InInstanceData)
		: ShapeFilterData(InShapeFilter)
		, InstanceData(InInstanceData)
	{
	}

	const FInstanceData& FCombinedShapeFilterData::GetInstanceData() const
	{
		return InstanceData;
	}

	void FCombinedShapeFilterData::SetInstanceData(const FInstanceData& InData)
	{
		InstanceData = InData;
	}

	const FShapeFilterData& FCombinedShapeFilterData::GetShapeFilterData() const
	{
		return ShapeFilterData;
	}

	void FCombinedShapeFilterData::SetShapeFilterData(const FShapeFilterData& InData)
	{
		ShapeFilterData = InData;
	}

	bool FCombinedShapeFilterData::IsValid() const
	{
		return ShapeFilterData.IsValid() || InstanceData.IsValid();
	}

	FShapeFilterData FShapeFilterBuilder::BuildLegacyShapeFilter(const uint8 InChannelIndex, const uint32 InQueryOverlapChannels, const uint32 InQueryBlockChannels, const uint32 InSimBlockChannels, const uint8 InMaskFilter, const EFilterFlags InFilterFlags)
	{
		// (Mask Filter 6 bits) | (Channel Index 5 bits) | (Flags 21 bits)
		FShapeFilterData Result;
		Result.QueryBlockChannels = InQueryBlockChannels;
		Result.QueryOverlapChannels = InQueryOverlapChannels;
		Result.SimBlockChannels = InSimBlockChannels;
		Result.Word3 = FilterFlagsMask & (uint32)InFilterFlags;
		Result.Word3 |= CollisionChannelMask & ((uint32)InChannelIndex << CollisionChannelBitsOffset);
		Result.Word3 |= MaskFilterMask & ((uint32)InMaskFilter << MaskFilterBitsOffset);
		Result.SimWord3 = Result.Word3;
		return Result;
	}

	FShapeFilterData FShapeFilterBuilder::BuildLegacyBlockAllSimShapeFilter(const uint8 InChannelIndex, const uint8 InMaskFilter, const EFilterFlags InFilterFlags)
	{
		return BuildLegacyShapeFilter(InChannelIndex, 0, 0, (uint32)-1, 0, InFilterFlags);
	}

	FShapeFilterData FShapeFilterBuilder::BuildFromLegacyQueryFilter(const FCollisionFilterData& QueryFilter)
	{
		FShapeFilterData Result;
		Result.QueryBlockChannels = QueryFilter.Word1;
		Result.QueryOverlapChannels = QueryFilter.Word2;
		Result.Word3 = QueryFilter.Word3;
		Result.SimWord3 = QueryFilter.Word3;
		return Result;
	}

	FShapeFilterData FShapeFilterBuilder::BuildFromLegacySimFilter(const FCollisionFilterData& SimFilter)
	{
		FShapeFilterData Result;
		Result.SimBlockChannels = SimFilter.Word1;
		Result.Word3 = SimFilter.Word3;
		Result.SimWord3 = SimFilter.Word3;
		return Result;
	}

	FCombinedShapeFilterData FShapeFilterBuilder::BuildFromLegacyShapeFilter(const FCollisionFilterData& QueryFilterData, const FCollisionFilterData& SimFilterData)
	{
		FCombinedShapeFilterData CombinedShapeFilterData;
		CombinedShapeFilterData.InstanceData.ActorId = QueryFilterData.Word0;
		CombinedShapeFilterData.InstanceData.ComponentId = SimFilterData.Word2;
		CombinedShapeFilterData.InstanceData.BodyIndex = SimFilterData.Word0;

		CombinedShapeFilterData.ShapeFilterData.QueryBlockChannels = QueryFilterData.Word1;
		CombinedShapeFilterData.ShapeFilterData.QueryOverlapChannels = QueryFilterData.Word2;
		CombinedShapeFilterData.ShapeFilterData.Word3 = QueryFilterData.Word3;
		CombinedShapeFilterData.ShapeFilterData.SimBlockChannels = SimFilterData.Word1;
		CombinedShapeFilterData.ShapeFilterData.SimWord3 = SimFilterData.Word3;

		return CombinedShapeFilterData;
	}

	void FShapeFilterBuilder::SetLegacyShapeQueryFilter(FCombinedShapeFilterData& CombinedShapeFilterData, const FCollisionFilterData& QueryFilterData)
	{
		const FCollisionFilterData SimFilterData = GetLegacyShapeSimFilter(CombinedShapeFilterData);
		CombinedShapeFilterData = BuildFromLegacyShapeFilter(QueryFilterData, SimFilterData);
	}

	void FShapeFilterBuilder::SetLegacyShapeSimFilter(FCombinedShapeFilterData& CombinedShapeFilterData, const FCollisionFilterData& SimFilterData)
	{
		const FCollisionFilterData QueryFilterData = GetLegacyShapeQueryFilter(CombinedShapeFilterData);
		CombinedShapeFilterData = BuildFromLegacyShapeFilter(QueryFilterData, SimFilterData);
	}

	void FShapeFilterBuilder::GetLegacyShapeFilter(const FCombinedShapeFilterData& CombinedShapeFilterData, FCollisionFilterData& OutQueryFilterData, FCollisionFilterData& OutSimFilterData)
	{
		OutQueryFilterData.Word0 = CombinedShapeFilterData.InstanceData.ActorId;
		OutSimFilterData.Word2 = CombinedShapeFilterData.InstanceData.ComponentId;
		OutSimFilterData.Word0 = CombinedShapeFilterData.InstanceData.BodyIndex;

		OutQueryFilterData.Word1 = CombinedShapeFilterData.ShapeFilterData.QueryBlockChannels;
		OutQueryFilterData.Word2 = CombinedShapeFilterData.ShapeFilterData.QueryOverlapChannels;
		OutQueryFilterData.Word3 = CombinedShapeFilterData.ShapeFilterData.Word3;
		OutSimFilterData.Word1 = CombinedShapeFilterData.ShapeFilterData.SimBlockChannels;
		OutSimFilterData.Word3 = CombinedShapeFilterData.ShapeFilterData.SimWord3;
	}

	FCollisionFilterData FShapeFilterBuilder::GetLegacyShapeQueryFilter(const FCombinedShapeFilterData& CombinedShapeFilterData)
	{
		FCollisionFilterData QueryFilter, SimFilter;
		GetLegacyShapeFilter(CombinedShapeFilterData, QueryFilter, SimFilter);
		return QueryFilter;
	}

	FCollisionFilterData FShapeFilterBuilder::GetLegacyShapeSimFilter(const FCombinedShapeFilterData& CombinedShapeFilterData)
	{
		FCollisionFilterData QueryFilter, SimFilter;
		GetLegacyShapeFilter(CombinedShapeFilterData, QueryFilter, SimFilter);
		return SimFilter;
	}

	FQueryFilterData FQueryFilterBuilder::CreateLegacyObjectTypeFilter(const uint32 ObjectTypesToQuery, const bool bMultiTrace, const uint8 IgnoreMask, const EFilterFlags FilterFlags)
	{
		const uint8 MultiTraceValue = bMultiTrace ? 0x1 : 0x0;
		FQueryFilterData Result;
		Result.Word0 = 0x0;
		Result.Word1 = ObjectTypesToQuery;
		Result.Word2 = 0;
		Result.SetMaskChannelAndFlags(IgnoreMask, MultiTraceValue, FilterFlags);
		return Result;
	}

	FQueryFilterData FQueryFilterBuilder::CreateLegacyTraceFilter(const uint8 ChannelIndex, const uint32 BlockChannelMask, const uint32 OverlapChannelMask, const uint8 IgnoreMask, const EFilterFlags FilterFlags)
	{
		FQueryFilterData Result;
		Result.Word0 = 0x1;
		Result.Word1 = BlockChannelMask;
		Result.Word2 = OverlapChannelMask;
		Result.SetMaskChannelAndFlags(IgnoreMask, ChannelIndex, FilterFlags);
		return Result;
	}

	FQueryFilterData FQueryFilterBuilder::BuildFromLegacyQueryFilter(const FCollisionFilterData& QueryFilterData)
	{
		FQueryFilterData Result;
		Result.Word0 = QueryFilterData.Word0;
		Result.Word1 = QueryFilterData.Word1;
		Result.Word2 = QueryFilterData.Word2;
		Result.Word3 = QueryFilterData.Word3;
		return Result;
	}

	FCollisionFilterData FQueryFilterBuilder::GetLegacyQueryFilter(const FQueryFilterData& QueryFilterData)
	{
		FCollisionFilterData Result;
		Result.Word0 = QueryFilterData.Word0;
		Result.Word1 = QueryFilterData.Word1;
		Result.Word2 = QueryFilterData.Word2;
		Result.Word3 = QueryFilterData.Word3;
		return Result;
	}
} // namespace Chaos::Filter
