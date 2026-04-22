// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "MaterialCacheAttribute.h"
#include "MaterialCacheVirtualTextureTag.generated.h"

#define UE_API ENGINE_API

UCLASS(MinimalAPI, Experimental)
class UMaterialCacheVirtualTextureTag : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** All stored attributes */
	UPROPERTY(EditAnywhere, Category="Material Cache")
	TArray<EMaterialCacheAttribute> Attributes;

	/** Optional, general multiplier to the tile counts of virtual textures using this tag */
	UPROPERTY(EditAnywhere, Category="Material Cache")
	FIntPoint TileCountMultiplier = FIntPoint(1, 1);

	/** Generated, the packed runtime layers of this tag */
	UPROPERTY(VisibleAnywhere, Category="Material Cache", AdvancedDisplay)
	TArray<FMaterialCacheLayer> RuntimeLayers;

	/** Internal guid for associations */
	UPROPERTY(VisibleAnywhere, Category="Material Cache", AdvancedDisplay)
	FGuid Guid = FGuid::NewGuid();

public:
	/** Get the thread-safe runtime layout of this tag */
	UE_API FMaterialCacheTagLayout GetRuntimeLayout();

public: /** UObject */
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

private:
	void PackRuntimeLayers();

};

#undef UE_API
