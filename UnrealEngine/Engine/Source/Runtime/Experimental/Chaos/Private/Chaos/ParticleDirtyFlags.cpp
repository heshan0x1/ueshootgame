// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ParticleDirtyFlags.h"

namespace Chaos
{
	const FCollisionFilterData& FCollisionData::GetQueryData() const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return QueryData;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void FCollisionData::SetQueryData(const FCollisionFilterData& InQueryData)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		QueryData = InQueryData;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	const FCollisionFilterData& FCollisionData::GetSimData() const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return SimData;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void FCollisionData::SetSimData(const FCollisionFilterData& InSimData)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SimData = InSimData;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	Chaos::Filter::FShapeFilterData FCollisionData::GetShapeFilterData() const
	{
		return GetCombinedShapeFilterData().GetShapeFilterData();
	}

	void FCollisionData::SetShapeFilterData(const Chaos::Filter::FShapeFilterData& ShapeFilter)
	{
		Chaos::Filter::FCombinedShapeFilterData CombinedShapeFilter = GetCombinedShapeFilterData();
		CombinedShapeFilter.SetShapeFilterData(ShapeFilter);
		SetCombinedShapeFilterData(CombinedShapeFilter);
	}

	Chaos::Filter::FInstanceData FCollisionData::GetFilterInstanceData() const
	{
		return GetCombinedShapeFilterData().GetInstanceData();
	}

	void FCollisionData::SetFilterInstanceData(const Chaos::Filter::FInstanceData& InstanceData)
	{
		Chaos::Filter::FCombinedShapeFilterData CombinedShapeFilter = GetCombinedShapeFilterData();
		CombinedShapeFilter.SetInstanceData(InstanceData);
		SetCombinedShapeFilterData(CombinedShapeFilter);
	}

	Chaos::Filter::FCombinedShapeFilterData FCollisionData::GetCombinedShapeFilterData() const
	{
		return Chaos::Filter::FShapeFilterBuilder::BuildFromLegacyShapeFilter(GetQueryData(), GetSimData());
	}

	void FCollisionData::SetCombinedShapeFilterData(const Chaos::Filter::FCombinedShapeFilterData& CombinedShapeFilter)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Chaos::Filter::FShapeFilterBuilder::GetLegacyShapeFilter(CombinedShapeFilter, QueryData, SimData);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
} // namespace Chaos
