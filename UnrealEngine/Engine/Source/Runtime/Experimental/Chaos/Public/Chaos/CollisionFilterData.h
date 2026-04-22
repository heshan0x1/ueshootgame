// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosArchive.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

// Supported flags for Chaos filter, shares some flags with EPhysXFilterDataFlags
// Shared flags should be kept in sync until unified. @see EPhysXFilterDataFlags
// #TODO unify filter builder with this flag list

namespace Chaos
{
	enum class EFilterFlags : uint8
	{
		None					= 0b00000000,
		SimpleCollision			= 0b00000001,	// The shape is used for simple collision
		ComplexCollision		= 0b00000010,	// The shape is used for complex (trimesh) collision
		CCD						= 0b00000100,	// Unused - present for compatibility. CCD handled per-particle in Chaos
		ContactNotify			= 0b00001000,	// Whether collisions with this shape should be reported back to the game thread
		StaticShape				= 0b00010000,	// Unused - present for compatibility
		ModifyContacts			= 0b00100000,	// Unused - present for compatibility, whether to allow contact modification, handled in Chaos callbacks now
		KinematicKinematicPairs	= 0b01000000,	// Unused - present for compatibility, whether to generate KK pairs, Chaos never generates KK pairs
		All						= 0xFF,
	};
	ENUM_CLASS_FLAGS(EFilterFlags);

	inline const TCHAR* LexToString(EFilterFlags FilterFlag)
	{
		switch (FilterFlag)
		{
			case EFilterFlags::None: return TEXT("None");
			case EFilterFlags::SimpleCollision: return TEXT("SimpleCollision");
			case EFilterFlags::ComplexCollision: return TEXT("ComplexCollision");
			case EFilterFlags::CCD: return TEXT("CCD");
			case EFilterFlags::ContactNotify: return TEXT("ContactNotify");
			case EFilterFlags::StaticShape: return TEXT("StaticShape");
			case EFilterFlags::ModifyContacts: return TEXT("ModifyContacts");
			case EFilterFlags::KinematicKinematicPairs: return TEXT("KinematicKinematicPairs");
			case EFilterFlags::All: return TEXT("All");
			default: return TEXT("Invalid");
		}
	}
}

struct FCollisionFilterData
{
	uint32 Word0;
	uint32 Word1;
	uint32 Word2;
	uint32 Word3;

	FORCEINLINE FCollisionFilterData()
	{
		Word0 = Word1 = Word2 = Word3 = 0;
	}

	bool HasFlag(Chaos::EFilterFlags InFlag) const
	{
		const uint32 FilterFlags = (Word3 & 0xFFFFFF);
		return FilterFlags & static_cast<uint32>(InFlag);
	}

	friend inline bool operator!=(const FCollisionFilterData& A, const FCollisionFilterData& B)
	{
		return A.Word0!=B.Word0 || A.Word1!=B.Word1 || A.Word2!=B.Word2 || A.Word3!=B.Word3;
	}
};

inline Chaos::FChaosArchive& operator<<(Chaos::FChaosArchive& Ar, FCollisionFilterData& Filter)
{
	Ar << Filter.Word0 << Filter.Word1 << Filter.Word2 << Filter.Word3;
	return Ar;
}

namespace Chaos::Filter
{
	struct FCombinedShapeFilterData;
	struct FQueryFilterData;

	struct FInstanceData
	{
		FInstanceData() = default;
		CHAOS_API FInstanceData(const uint32 ActorId, const uint32 ComponentId);

		bool operator==(const FInstanceData&) const = default;
		bool operator!=(const FInstanceData&) const = default;

		CHAOS_API bool IsValid() const;

		CHAOS_API uint32 GetActorId() const;
		CHAOS_API void SetActorId(const uint32 InActorId);

		UE_INTERNAL CHAOS_API uint32 GetComponentId() const;
		UE_INTERNAL CHAOS_API void SetComponentId(const uint32 InComponentId);

	private:
		friend class FShapeFilterBuilder;
		friend FCombinedShapeFilterData;

		uint32 ActorId = 0;
		uint32 ComponentId = 0;
		// TODO @ JoshD: Remove if possible
		uint32 BodyIndex = 0;
	};

	struct FShapeFilterData
	{
		FShapeFilterData() = default;

		bool operator==(const FShapeFilterData&) const = default;
		bool operator!=(const FShapeFilterData&) const = default;

		CHAOS_API bool IsValid() const;

		CHAOS_API EFilterFlags GetFlags() const;
		CHAOS_API void SetFlags(EFilterFlags InFlags);
		CHAOS_API bool HasFlag(EFilterFlags InFlag) const;

		CHAOS_API uint8 GetMaskFilter() const;
		CHAOS_API void SetMaskFilter(uint8 MaskFilter);

		CHAOS_API uint8 GetCollisionChannelIndex() const;
		CHAOS_API uint64 GetCollisionChannelMask() const;

		CHAOS_API uint64 GetQueryBlockChannels() const;
		CHAOS_API uint64 GetQueryOverlapChannels() const;
		CHAOS_API uint64 GetSimBlockChannels() const;

	private:
		friend class FShapeFilterBuilder;
		friend FQueryFilterData;
		friend FCombinedShapeFilterData;

		uint32 QueryBlockChannels = 0;
		uint32 QueryOverlapChannels = 0;
		uint32 SimBlockChannels = 0;
		uint32 Word3 = 0;
		// TODO @ JoshD: Remove this eventually. This is kept here for legacy conversions until usage is cleaned up.
		uint32 SimWord3 = 0;
	};

	struct FQueryFilterData
	{
		enum EQueryType
		{
			Channel = 0,
			ObjectType,
		};

		FQueryFilterData() = default;

		bool operator==(const FQueryFilterData&) const = default;
		bool operator!=(const FQueryFilterData&) const = default;

		CHAOS_API bool IsValid() const;

		CHAOS_API EQueryType GetQueryType() const;

		CHAOS_API EFilterFlags GetFlags() const;
		CHAOS_API void SetFlags(EFilterFlags InFlags);
		CHAOS_API bool HasFlag(EFilterFlags InFlag) const;

		CHAOS_API uint8 GetIgnoreMask() const;

		// Channel API
		CHAOS_API uint8 GetCollisionChannelIndex() const;
		CHAOS_API uint64 GetCollisionChannelMask() const;
		CHAOS_API uint64 GetBlockChannels() const;
		CHAOS_API uint64 GetOverlapChannels() const;

		// ObjectType API
		CHAOS_API uint64 GetObjectTypesToQueryMask() const;
		CHAOS_API bool IsMultiQuery() const;

	private:
		friend class FQueryFilterBuilder;

		void SetMaskChannelAndFlags(const uint8 IgnoreMask, const uint8 ChannelIndex, const EFilterFlags FilterFlags);

		uint32 Word0 = 0;
		uint32 Word1 = 0;
		uint32 Word2 = 0;
		uint32 Word3 = 0;
	};

	struct FCombinedShapeFilterData
	{
		FCombinedShapeFilterData() = default;
		CHAOS_API FCombinedShapeFilterData(const FShapeFilterData& InShapeFilter, const FInstanceData& InInstanceData);

		bool operator==(const FCombinedShapeFilterData&) const = default;
		bool operator!=(const FCombinedShapeFilterData&) const = default;

		CHAOS_API const FInstanceData& GetInstanceData() const;
		CHAOS_API void SetInstanceData(const FInstanceData& InData);

		CHAOS_API const FShapeFilterData& GetShapeFilterData() const;
		CHAOS_API void SetShapeFilterData(const FShapeFilterData& InData);

		CHAOS_API bool IsValid() const;

	private:
		friend class FShapeFilterBuilder;
		FShapeFilterData ShapeFilterData;
		FInstanceData InstanceData;
	};

	class UE_INTERNAL FShapeFilterBuilder
	{
	public:
		UE_INTERNAL CHAOS_API static FShapeFilterData BuildLegacyShapeFilter(const uint8 InChannelIndex, const uint32 InQueryOverlapChannels, const uint32 InQueryBlockChannels, const uint32 InSimBlockChannels, const uint8 InMaskFilter = 0, const EFilterFlags InFilterFlags = EFilterFlags::None);
		UE_INTERNAL CHAOS_API static FShapeFilterData BuildLegacyBlockAllSimShapeFilter(const uint8 InChannelIndex = 0, const uint8 InMaskFilter = 0, const EFilterFlags InFilterFlags = EFilterFlags::All);

		UE_INTERNAL CHAOS_API static FShapeFilterData BuildFromLegacyQueryFilter(const FCollisionFilterData& QueryFilter);
		UE_INTERNAL CHAOS_API static FShapeFilterData BuildFromLegacySimFilter(const FCollisionFilterData& SimFilter);
		UE_INTERNAL CHAOS_API static FCombinedShapeFilterData BuildFromLegacyShapeFilter(const FCollisionFilterData& QueryFilterData, const FCollisionFilterData& SimFilterData);

		UE_INTERNAL CHAOS_API static void SetLegacyShapeQueryFilter(FCombinedShapeFilterData& CombinedShapeFilterData, const FCollisionFilterData& QueryFilterData);
		UE_INTERNAL CHAOS_API static void SetLegacyShapeSimFilter(FCombinedShapeFilterData& CombinedShapeFilterData, const FCollisionFilterData& SimFilterData);

		UE_INTERNAL CHAOS_API static void GetLegacyShapeFilter(const FCombinedShapeFilterData& CombinedShapeFilterData, FCollisionFilterData& OutQueryFilterData, FCollisionFilterData& OutSimFilterData);
		UE_INTERNAL CHAOS_API static FCollisionFilterData GetLegacyShapeQueryFilter(const FCombinedShapeFilterData& CombinedShapeFilterData);
		UE_INTERNAL CHAOS_API static FCollisionFilterData GetLegacyShapeSimFilter(const FCombinedShapeFilterData& CombinedShapeFilterData);
	private:
	};

	class UE_INTERNAL FQueryFilterBuilder
	{
	public:
		UE_INTERNAL CHAOS_API static FQueryFilterData CreateLegacyObjectTypeFilter(const uint32 ObjectTypesToQuery, const bool bMultiTrace, const uint8 IgnoreMask = 0, const EFilterFlags FilterFlags = EFilterFlags::None);
		UE_INTERNAL CHAOS_API static FQueryFilterData CreateLegacyTraceFilter(const uint8 ChannelIndex, const uint32 BlockChannelMask, const uint32 OverlapChannelMask, const uint8 IgnoreMask = 0, const EFilterFlags FilterFlags = EFilterFlags::None);

		UE_INTERNAL CHAOS_API static FQueryFilterData BuildFromLegacyQueryFilter(const FCollisionFilterData& QueryFilterData);
		UE_INTERNAL CHAOS_API static FCollisionFilterData GetLegacyQueryFilter(const FQueryFilterData& QueryFilterData);
	private:
	};
} // namespace Chaos::Filter
