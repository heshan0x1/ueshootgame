// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsolePhysicalUnitToUnitNameLabel.h"

namespace UE::DMX
{
	const FName& FDMXControlConsolePhysicalUnitToUnitNameLabel::GetNameLabel(EDMXGDTFPhysicalUnit PhysicalUnit)
	{
		return PhysicalUnitToUnitNameLabelMap.FindChecked(PhysicalUnit);
	}

	const TMap<EDMXGDTFPhysicalUnit, FName> FDMXControlConsolePhysicalUnitToUnitNameLabel::PhysicalUnitToUnitNameLabelMap
	{
		{ EDMXGDTFPhysicalUnit::None, "normal"},
		{ EDMXGDTFPhysicalUnit::Percent, "%" },
		{ EDMXGDTFPhysicalUnit::Length, "m"},
		{ EDMXGDTFPhysicalUnit::Mass, "kg" },
		{ EDMXGDTFPhysicalUnit::Time, "s" },
		{ EDMXGDTFPhysicalUnit::Temperature, "K" },
		{ EDMXGDTFPhysicalUnit::LuminousIntensity, "cd" },
		{ EDMXGDTFPhysicalUnit::Angle, "deg"},
		{ EDMXGDTFPhysicalUnit::Force, "N" },
		{ EDMXGDTFPhysicalUnit::Frequency, "Hz" },
		{ EDMXGDTFPhysicalUnit::Current, "A" },
		{ EDMXGDTFPhysicalUnit::Voltage, "V" },
		{ EDMXGDTFPhysicalUnit::Power, "W" },
		{ EDMXGDTFPhysicalUnit::Energy, "J" },
		{ EDMXGDTFPhysicalUnit::Area, "m2" },
		{ EDMXGDTFPhysicalUnit::Volume, "m3" },
		{ EDMXGDTFPhysicalUnit::Speed, "m/s" },
		{ EDMXGDTFPhysicalUnit::Acceleration, "m/s2" },
		{ EDMXGDTFPhysicalUnit::AngularSpeed, "deg/s" },
		{ EDMXGDTFPhysicalUnit::AngularAccc, "deg/s2" },
		{ EDMXGDTFPhysicalUnit::WaveLength, "nm" },
		{ EDMXGDTFPhysicalUnit::ColorComponent, "%" },
		{ EDMXGDTFPhysicalUnit::MaxEnumValue, "-" }
	};
}
