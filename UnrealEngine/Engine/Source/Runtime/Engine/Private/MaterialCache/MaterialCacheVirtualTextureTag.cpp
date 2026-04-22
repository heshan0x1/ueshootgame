// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCache/MaterialCacheVirtualTextureTag.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialCacheVirtualTextureTag)

UMaterialCacheVirtualTextureTag::UMaterialCacheVirtualTextureTag(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Pack default layers
	Attributes = TArrayView<EMaterialCacheAttribute>(DefaultMaterialCacheAttributes);
	PackRuntimeLayers();
}

FMaterialCacheTagLayout UMaterialCacheVirtualTextureTag::GetRuntimeLayout()
{
	FMaterialCacheTagLayout Out;
	Out.Guid = Guid;
	Out.Layers = RuntimeLayers;
	return Out;
}

#if WITH_EDITOR
void UMaterialCacheVirtualTextureTag::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	PackRuntimeLayers();

	// TODO: Mark dependent materials as out of date!
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UMaterialCacheVirtualTextureTag::PackRuntimeLayers()
{
	// Repack with new attributes
	RuntimeLayers.Reset();
	PackMaterialCacheAttributeLayers(TArrayView<EMaterialCacheAttribute>(Attributes), RuntimeLayers);

	// Must have at least one attribute
	if (RuntimeLayers.IsEmpty())
	{
		UE_LOG(LogVirtualTexturing, Error, TEXT("Invalid material cache tag, must have at least one layer."));
	}

	// Validate against physical limits
	if (RuntimeLayers.Num() > MaterialCacheMaxRuntimeLayers)
	{
		UE_LOG(LogVirtualTexturing, Error, TEXT("Invalid material cache tag, too many layers (max %u). Consider removing attributes or splitting it into separate tags."), MaterialCacheMaxRuntimeLayers);
		RuntimeLayers.Empty();
	}
}
