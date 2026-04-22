// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

class FCluster;
struct FMaterialRange;

void BuildVertReuseBatches(TArray<FCluster>& Clusters);
uint32 CalcVertReuseBatchInfoSize(const TArrayView<const FMaterialRange>& MaterialRanges);
void PackVertReuseBatchInfo(const TArrayView<const FMaterialRange>& MaterialRanges, TArray<uint32>& OutVertReuseBatchInfo);

}
