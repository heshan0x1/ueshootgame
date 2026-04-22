// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingSimulationInterface.h"

//==============================================================================
// IClotingSimulationContext
//==============================================================================

IClothingSimulationContext::IClothingSimulationContext() = default;
IClothingSimulationContext::~IClothingSimulationContext() = default;

//==============================================================================
// IClothingSimulationInterface
//==============================================================================

IClothingSimulationInterface::IClothingSimulationInterface() = default;
IClothingSimulationInterface::~IClothingSimulationInterface() = default;

//==============================================================================
// IClothingSimulation
//==============================================================================

IClothingSimulation::IClothingSimulation()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bIsLegacyInterface = true;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
IClothingSimulation::~IClothingSimulation() = default;
