// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborOptimizedNetworkLoader.h"
#include "NearestNeighborOptimizedNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NearestNeighborOptimizedNetworkLoader)

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UNearestNeighborOptimizedNetworkLoader::SetOptimizedNetwork(UNearestNeighborOptimizedNetwork* InNetwork)
{
	Network = InNetwork;
}

UNearestNeighborOptimizedNetwork* UNearestNeighborOptimizedNetworkLoader::GetOptimizedNetwork() const
{
	return Network;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
