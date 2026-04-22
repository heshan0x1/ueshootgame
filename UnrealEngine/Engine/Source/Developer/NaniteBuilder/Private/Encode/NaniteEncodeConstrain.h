// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Nanite
{

class FCluster;
struct FClusterGroup;

void ConstrainClusters(TArray<FClusterGroup>& ClusterGroups, TArray<FCluster>& Clusters);
void VerifyClusterConstraints(const TArray<FCluster>& Clusters);

}
