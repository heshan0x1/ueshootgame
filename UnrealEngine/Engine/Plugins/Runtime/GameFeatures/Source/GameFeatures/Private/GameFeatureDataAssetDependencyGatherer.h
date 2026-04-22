// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "AssetRegistry/AssetDependencyGatherer.h"

class FGameFeatureDataAssetDependencyGatherer : public IAssetDependencyGatherer
{
public:
	
	FGameFeatureDataAssetDependencyGatherer() = default;
	virtual ~FGameFeatureDataAssetDependencyGatherer() = default;

	virtual void GatherDependencies(FGatherDependenciesContext& Context) const override;
};

#endif
