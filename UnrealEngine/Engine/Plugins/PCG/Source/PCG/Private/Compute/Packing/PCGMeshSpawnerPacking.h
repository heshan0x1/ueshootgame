// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPCGSettings;
struct FPCGAttributePropertyInputSelector;
struct FPCGContext;
struct FPCGDataCollectionDesc;

namespace PCGMeshSpawnerPackingHelpers
{
	/** Compute how attributes can be packed to custom floats. */
	void ComputeCustomFloatPacking(
		FPCGContext* InContext,
		const UPCGSettings* InSettings,
		TConstArrayView<FPCGAttributePropertyInputSelector> InAttributeSelectors,
		const TSharedPtr<const FPCGDataCollectionDesc> InDataCollectionDescription,
		uint32& OutCustomFloatCount,
		TArray<FUint32Vector4>& OutAttributeIdOffsetStrides);
}
