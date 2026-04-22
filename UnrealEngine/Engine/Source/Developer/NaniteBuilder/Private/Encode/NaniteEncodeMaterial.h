// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

class FCluster;

uint32 CalcMaterialTableSize(const FCluster& Cluster);
uint32 PackMaterialInfo(const FCluster& Cluster, TArray<uint32>& OutMaterialTable, uint32 MaterialTableStartOffset);
	
void BuildMaterialRanges(TArray<FCluster>& Clusters);

void PrintMaterialRangeStats(const TArray<FCluster>& Clusters);
	
}
