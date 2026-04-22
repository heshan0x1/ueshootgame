// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

struct FIntermediateResources;
struct FInputAssemblyData;

bool AddAssemblyParts(FIntermediateResources& AssemblyResources, const FInputAssemblyData& AssemblyData);

} // namespace Nanite