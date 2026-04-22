// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "AssetRegistry/AssetDependencyGatherer.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"

class FExternalActorAssetDependencyGatherer : public IAssetDependencyGatherer
{
public:
	
	FExternalActorAssetDependencyGatherer() = default;
	virtual ~FExternalActorAssetDependencyGatherer() = default;

	virtual void GatherDependencies(FGatherDependenciesContext& Context) const override;
};

#endif