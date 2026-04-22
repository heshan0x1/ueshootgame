// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "GDTF/AttributeDefinitions/DMXGDTFPhysicalUnit.h"

namespace UE::DMX
{
	struct FDMXPhysicalUnitToDefaultValueRange
	{
		/** Returns the value range for specified physical unit */
		static const TPair<double, double>& GetValueRange(EDMXGDTFPhysicalUnit PhysicalUnit);

	private:
		/** A map to list all GDTF compliant Physical Units and their matching Physical Value default ranges. */
		static const TMap<EDMXGDTFPhysicalUnit, TPair<double, double>> PhysicalUnitToValueRangeMap;
	};
}
