// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Polygon/PCGPolygon2DUtils.h"

namespace PCGPolygon2DUtils
{

TArray<FPCGPinProperties> DefaultPolygonInputPinProperties()
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Polygon2D).SetRequiredPin();

	return PinProperties;
}

TArray<FPCGPinProperties> DefaultPolygonOutputPinProperties()
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Polygon2D);

	return PinProperties;
}

} // namespace PCGPolygon2DUtils
