// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "GDTF/AttributeDefinitions/DMXGDTFPhysicalUnit.h"

namespace UE::DMX
{
	struct FDMXControlConsolePhysicalUnitToUnitNameLabel
	{
		/** Returns the name label for the specified physical unit */
		static const FName& GetNameLabel(EDMXGDTFPhysicalUnit PhysicalUnit);

	private:
		/** A map to list all GDTF compliant Physical Units and their matching Physical Value default ranges. */
		static const TMap<EDMXGDTFPhysicalUnit, FName> PhysicalUnitToUnitNameLabelMap;
	};
}
