// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCache/MaterialCacheVirtualTexture.h"
#include "Components/PrimitiveComponent.h"
#include "EngineModule.h"
#include "UnrealEngine.h"
#include "Interfaces/ITargetPlatform.h"
#include "RendererInterface.h"
#include "TextureResource.h"
#include "VirtualTextureEnum.h"
#include "VirtualTexturing.h"
#include "MaterialCache/IMaterialCacheTagProvider.h"
#include "MaterialCache/MaterialCache.h"
#include "MaterialCache/MaterialCacheAttribute.h"
#include "MaterialCache/MaterialCacheMeshProcessor.h"
#include "MaterialCache/MaterialCacheStackProvider.h"
#include "MaterialCache/MaterialCacheVirtualTextureDescriptor.h"
#include "MaterialCache/MaterialCacheVirtualTextureRenderProxy.h"
#include "VT/VirtualTextureBuildSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialCacheVirtualTexture)

struct FMaterialCacheVirtualBaton
{
	UMaterialCacheVirtualTexture* SelfUnsafe     = nullptr;
	FSceneInterface*              SceneInterface = nullptr;
	IAllocatedVirtualTexture*     VirtualTexture = nullptr;
	FPrimitiveComponentId         PrimitiveComponentId;
	FGuid                         TagGuid;
};

class FMaterialCacheVirtualTextureResource : public FVirtualTexture2DResource
{
public:
	FMaterialCacheVirtualTextureResource(FSceneInterface* Scene, FPrimitiveComponentId InPrimitiveComponentId, const FMaterialCacheTagLayout& TagLayout, FIntPoint InTileCount, int32 InTileSize, int32 InTileBorderSize)
		: Scene(Scene)
		, PrimitiveComponentId(InPrimitiveComponentId)
		, TagLayout(TagLayout)
		, TileCount(InTileCount)
		, TileSize(InTileSize)
		, TileBorderSize(InTileBorderSize)
	{
		TextureName = TEXT("MaterialCacheVirtualTexture");
		
		MaxLevel = FMath::CeilLogTwo(FMath::Max(InTileCount.X, InTileCount.Y));

		// Share the page table across all physical textures
		bSinglePhysicalSpace = true;
	}

	virtual uint32 GetNumLayers() const override
	{
		return TagLayout.Layers.Num();
	}
	
	virtual EPixelFormat GetFormat(uint32 LayerIndex) const override
	{
		return TagLayout.Layers[LayerIndex].CompressedFormat;
	}
	
	virtual uint32 GetTileSize() const override
	{
		return TileSize;
	}
	
	virtual uint32 GetBorderSize() const override
	{
		return TileBorderSize;
	}
	
	virtual uint32 GetNumTilesX() const override
	{
		return TileCount.X;
	}
	
	virtual uint32 GetNumTilesY() const override
	{
		return TileCount.Y;
	}

	virtual uint32 GetNumMips() const override
	{
		return MaxLevel + 1;
	}

	virtual FIntPoint GetSizeInBlocks() const override
	{
		return 1;
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FSamplerStateInitializerRHI SamplerStateInitializer;
		SamplerStateInitializer.Filter = SF_Bilinear;
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		// Create underlying producer
		FVTProducerDescription ProducerDesc;
		ProducerDesc.Name               = TextureName;
		ProducerDesc.FullNameHash       = GetTypeHash(TextureName);
		ProducerDesc.bContinuousUpdate  = false;
		ProducerDesc.Dimensions         = 2;
		ProducerDesc.TileSize           = TileSize;
		ProducerDesc.TileBorderSize     = TileBorderSize;
		ProducerDesc.BlockWidthInTiles  = TileCount.X;
		ProducerDesc.BlockHeightInTiles = TileCount.Y;
		ProducerDesc.DepthInTiles       = 1u;
		ProducerDesc.MaxLevel           = MaxLevel;
		ProducerDesc.NumTextureLayers   = TagLayout.Layers.Num();
		ProducerDesc.NumPhysicalGroups  = 1;
		ProducerDesc.Priority           = EVTProducerPriority::Normal;

		for (int32 LayerIndex = 0; LayerIndex < TagLayout.Layers.Num(); LayerIndex++)
		{
			ProducerDesc.LayerFormat[LayerIndex]        = TagLayout.Layers[LayerIndex].CompressedFormat;
			ProducerDesc.bIsLayerSRGB[LayerIndex]       = TagLayout.Layers[LayerIndex].bIsSRGB;
			ProducerDesc.PhysicalGroupIndex[LayerIndex] = 0;
		}

		IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
		
		// Register producer on page feedback
		IVirtualTexture* Producer = TagProvider->CreateProducer(Scene, PrimitiveComponentId, TagLayout, ProducerDesc);
		ProducerHandle = GetRendererModule().RegisterVirtualTextureProducer(RHICmdList, ProducerDesc, Producer);
	}

private:
	/** Owning scene, lifetime tied to the parent game virtual texture */
	FSceneInterface* Scene = nullptr;
	
	/** Owning component id, lifetime tied to the parent game virtual texture */
	FPrimitiveComponentId PrimitiveComponentId;
	
	/** Physical formats */
	FMaterialCacheTagLayout TagLayout;

	/** Tiled properties */
	FIntPoint TileCount;
	uint32    TileSize       = 0;
	uint32    TileBorderSize = 0;
	uint32    MaxLevel       = 0;
	uint32    NumSourceMips  = 1;
};

UMaterialCacheVirtualTexture::UMaterialCacheVirtualTexture(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	VirtualTextureStreaming = false;

#if WITH_EDITORONLY_DATA
	CompressionNone = true;
	CompressionForceAlpha = true;
#endif // WITH_EDITORONLY_DATA
}

UMaterialCacheVirtualTexture::~UMaterialCacheVirtualTexture()
{
	
}

void UMaterialCacheVirtualTexture::CreateSceneProxy()
{
	// Get the resource on the game thread
	FVirtualTexture2DResource* VTResource = GetVirtualTexture2DResource();
	if (!VTResource)
	{
		return;
	}

	// May not exist if headless
	FSceneInterface* Scene = GetScene();
	if (!Scene)
	{
		return;
	}

	FPrimitiveComponentId PrimitiveComponentId = OwningComponent->GetSceneData().PrimitiveSceneId;

	FMaterialCacheTagLayout Layout = GetRuntimeLayout();
	
	ENQUEUE_RENDER_COMMAND(AcquireVT)([this, VTResource, Layout = MoveTemp(Layout), Scene, PrimitiveComponentId](FRHICommandListImmediate&)
	{
		// Must exist
		IAllocatedVirtualTexture* AllocatedVT = VTResource->GetAllocatedVT();
		if (!ensure(AllocatedVT))
		{
			return;
		}
		
		IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
		TagProvider->Register(Scene, PrimitiveComponentId, Layout, AllocatedVT);
	});
}

FMaterialCacheVirtualTextureRenderProxy* UMaterialCacheVirtualTexture::CreateRenderProxy(uint32 UVCoordinateIndex)
{
	FVirtualTexture2DResource* VTResource = GetVirtualTexture2DResource();
	if (!VTResource)
	{
		return nullptr;
	}
	
	FMaterialCacheVirtualTextureRenderProxy* Proxy = new FMaterialCacheVirtualTextureRenderProxy();
	Proxy->PrimitiveCID      = OwningComponent->GetSceneData().PrimitiveSceneId;
	Proxy->UVCoordinateIndex = UVCoordinateIndex;

	if (UMaterialCacheVirtualTextureTag* TagHandle = Tag.Get())
	{
		Proxy->TagGuid = TagHandle->Guid;
	}
	
	// Stack providers are optional
	if (UMaterialCacheStackProvider* StackProvider = MaterialStackProvider.Get())
	{
		Proxy->StackProviderRenderProxy = TUniquePtr<FMaterialCacheStackProviderRenderProxy>(StackProvider->CreateRenderProxy());
	}

	// Render thread initialization
	ENQUEUE_RENDER_COMMAND(GetBackbufferFormatCmd)([Proxy, VTResource](FRHICommandListImmediate&)
	{
		Proxy->TextureDescriptor = PackMaterialCacheTextureDescriptor(VTResource, Proxy->UVCoordinateIndex);
	});

	return Proxy;
}

FMaterialCacheTagLayout UMaterialCacheVirtualTexture::GetRuntimeLayout() const
{
	FMaterialCacheTagLayout Layout;

	// Tags are optional
	if (UMaterialCacheVirtualTextureTag* TagHandle = Tag.Get())
	{
		Layout = TagHandle->GetRuntimeLayout();
	}

	// If there's no valid layers, or its invalid, assume defaults
	if (!Layout.Layers.Num())
	{
		PackMaterialCacheAttributeLayers(DefaultMaterialCacheAttributes, Layout.Layers);
	}

	return Layout;
}

void UMaterialCacheVirtualTexture::Flush()
{
	// Get the resource on the game thread
	FVirtualTexture2DResource* VTResource = GetVirtualTexture2DResource();
	if (!VTResource)
	{
		return;
	}

	// Flush the full UV-range
	ENQUEUE_RENDER_COMMAND(MaterialCacheFlush)([VTResource](FRHICommandListBase&)
	{
		if (IAllocatedVirtualTexture* AllocatedVT = VTResource->GetAllocatedVT())
		{
			GetRendererModule().FlushVirtualTextureCache(AllocatedVT, FVector2f(0, 0), FVector2f(1, 1));
		}
	});
}

void UMaterialCacheVirtualTexture::Unregister()
{
	// May not exist if headless
	FSceneInterface* Scene = GetScene();
	if (!Scene)
	{
		return;
	}

	// Get the resource on the game thread
	FVirtualTexture2DResource* VTResource = GetVirtualTexture2DResource();
	if (!VTResource)
	{
		return;
	}
	
	FPrimitiveComponentId PrimitiveComponentId = OwningComponent->GetSceneData().PrimitiveSceneId;
	
	FGuid TagGuid;
	if (UMaterialCacheVirtualTextureTag* TagHandle = Tag.Get())
	{
		TagGuid = TagHandle->Guid;
	}
	
	ENQUEUE_RENDER_COMMAND(ReleaseVT)([this, VTResource, TagGuid, Scene, PrimitiveComponentId](FRHICommandListImmediate&)
	{
		IAllocatedVirtualTexture* AllocatedVT = VTResource->GetAllocatedVT();
		if (!ensure(AllocatedVT))
		{
			return;
		}
		
		IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
		TagProvider->Unregister(Scene, PrimitiveComponentId, TagGuid, AllocatedVT);

		// Remove pending Batons
		if (RTDestructionBaton)
		{
			GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(RTDestructionBaton);
			delete RTDestructionBaton;
		}
	});
}

FIntPoint UMaterialCacheVirtualTexture::GetRuntimeTileCount() const
{
	FIntPoint TaggedTileCount;
	
	if (UMaterialCacheVirtualTextureTag* TagHandle = Tag.Get())
	{
		TaggedTileCount = TileCount * TagHandle->TileCountMultiplier;
	}
	else
	{
		TaggedTileCount = TileCount;
	}

	return TaggedTileCount.ComponentMax(FIntPoint(1, 1));
}

void UMaterialCacheVirtualTexture::GetVirtualTextureBuildSettings(FVirtualTextureBuildSettings& OutSettings) const
{
	OutSettings.TileSize       = GetMaterialCacheTileWidth();
	OutSettings.TileBorderSize = GetMaterialCacheTileBorderWidth();
}

void UMaterialCacheVirtualTexture::UpdateResourceWithParams(EUpdateResourceFlags InFlags)
{	
	Super::UpdateResourceWithParams(InFlags);

	// Get the resource on the game thread
	FVirtualTexture2DResource* VTResource = GetVirtualTexture2DResource();
	if (!VTResource)
	{
		return;
	}

	// May not exist if headless
	FSceneInterface* Scene = GetScene();
	if (!Scene)
	{
		return;
	}

	// Null tags are allowed
	FGuid TagGuid;
	if (UMaterialCacheVirtualTextureTag* TagHandle = Tag.Get())
	{
		TagGuid = TagHandle->Guid;
	}
	
	FPrimitiveComponentId PrimitiveComponentId = OwningComponent->GetSceneData().PrimitiveSceneId;
	ENQUEUE_RENDER_COMMAND(AcquireVT)([this, TagGuid, VTResource, Scene, PrimitiveComponentId](FRHICommandListImmediate&)
	{
		// If a previous virtual texture was registered, remove it
		if (RTVirtualTextureStalePtr)
		{
			IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
			TagProvider->Unregister(Scene, PrimitiveComponentId, TagGuid, RTVirtualTextureStalePtr);
			RTVirtualTextureStalePtr = nullptr;
		}

		// Remove previous baton, if any
		if (RTDestructionBaton)
		{
			GetRendererModule().RemoveAllVirtualTextureProducerDestroyedCallbacks(RTDestructionBaton);
			delete RTDestructionBaton;
		}
			
		// Acquire or allocate
		IAllocatedVirtualTexture* AllocatedVT = VTResource->AcquireAllocatedVT();
		if (!ensure(AllocatedVT))
		{
			return;
		}

		// Keep the handle around
		RTVirtualTextureStalePtr = AllocatedVT;

		// Baton for destruction
		RTDestructionBaton = new FMaterialCacheVirtualBaton();
		RTDestructionBaton->SelfUnsafe           = this;
		RTDestructionBaton->SceneInterface       = Scene;
		RTDestructionBaton->VirtualTexture       = AllocatedVT;
		RTDestructionBaton->PrimitiveComponentId = PrimitiveComponentId;
		RTDestructionBaton->TagGuid              = TagGuid;

		GetRendererModule().AddVirtualTextureProducerDestroyedCallback(
			AllocatedVT->GetProducerHandle(0),
			[](const FVirtualTextureProducerHandle&, void* InBaton)
			{
				const FMaterialCacheVirtualBaton* Baton = static_cast<const FMaterialCacheVirtualBaton*>(InBaton);
				
				IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
				TagProvider->Unregister(Baton->SceneInterface, Baton->PrimitiveComponentId, Baton->TagGuid, Baton->VirtualTexture);
				
				delete Baton;
			},
			RTDestructionBaton
		);
	});

	// Setup the needed proxies
	CreateSceneProxy();
}

EMaterialValueType UMaterialCacheVirtualTexture::GetMaterialType() const
{
	return MCT_TextureVirtual;
}

float UMaterialCacheVirtualTexture::GetSurfaceWidth() const
{
	return GetMaterialCacheTileWidth() * GetRuntimeTileCount().X;
}

float UMaterialCacheVirtualTexture::GetSurfaceHeight() const
{
	return GetMaterialCacheTileWidth() * GetRuntimeTileCount().Y;
}

uint32 UMaterialCacheVirtualTexture::GetSurfaceArraySize() const
{
	return 1;
}

float UMaterialCacheVirtualTexture::GetSurfaceDepth() const
{
	return 1;
}

ETextureClass UMaterialCacheVirtualTexture::GetTextureClass() const
{
	return ETextureClass::TwoD;
}

FTextureResource* UMaterialCacheVirtualTexture::CreateResource()
{
	check(IsInGameThread());
	
	if (!OwningComponent.Get())
	{
		UE_LOG(LogEngine, Error, TEXT("Material Cache Virtual Texture requires an owning component"));
		return nullptr;
	}

	if (!OwningComponent->GetScene())
	{
		return nullptr;
	}
	
	FVirtualTextureBuildSettings DefaultSettings;
	DefaultSettings.Init();
	GetVirtualTextureBuildSettings(DefaultSettings);

	return new FMaterialCacheVirtualTextureResource(
		OwningComponent->GetScene(),
		OwningComponent->GetPrimitiveSceneId(),
		GetRuntimeLayout(),
		GetRuntimeTileCount(),
		DefaultSettings.TileSize,
		DefaultSettings.TileBorderSize
	);
}

bool UMaterialCacheVirtualTexture::IsCurrentlyVirtualTextured() const
{
	return true;
}

FVirtualTexture2DResource* UMaterialCacheVirtualTexture::GetVirtualTexture2DResource()
{
	FTextureResource* Resource = GetResource();
	if (!Resource)
	{
		return nullptr;
	}
	
	return Resource->GetVirtualTexture2DResource();
}

FSceneInterface* UMaterialCacheVirtualTexture::GetScene()
{
	UPrimitiveComponent* Component = OwningComponent.Get();
	if (!Component)
	{
		return nullptr;
	}
	
	return Component->GetScene();
}

void FMaterialCacheVirtualTextureRenderProxy::Flush(FSceneInterface* Scene)
{
	IMaterialCacheTagProvider* TagProvider = GetRendererModule().GetMaterialCacheTagProvider();
	TagProvider->Flush(Scene, PrimitiveCID, TagGuid);
}
