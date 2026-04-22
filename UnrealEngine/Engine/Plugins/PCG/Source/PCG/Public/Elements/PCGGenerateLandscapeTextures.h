// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"

#include "RendererInterface.h"

#include "PCGGenerateLandscapeTextures.generated.h"

class ALandscapeProxy;
class FLandscapeGrassWeightExporter;
class ULandscapeComponent;
class ULandscapeGrassType;
class UPCGTextureData;
class UTexture;
struct IPooledRenderTarget;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGGenerateLandscapeTexturesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGGenerateLandscapeTexturesSettings();

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GenerateLandscapeTextures")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGGenerateLandscapeTexturesElement", "NodeTitle", "Generate Landscape Textures"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGGenerateLandscapeTexturesElement", "NodeTooltip", "Generates landscape height texture and grass maps on the GPU."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::GPU; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
#endif
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual bool IsInputPinRequiredByExecution(const UPCGPin* InPin) const override { return true; }
	virtual FPCGElementPtr CreateElement() const override;
#if WITH_EDITOR
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override;
#endif
	//~End UPCGSettings interface

public:
	/** Select which grass types to generate. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "!bOverrideFromInput"))
	TArray<FString> SelectedGrassTypes;

	/** Override grass types from input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideFromInput = false;

	/** Input attribute to pull grass type strings from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "bOverrideFromInput", PCG_DiscardPropertySelection, PCG_DiscardExtraSelection))
	FPCGAttributePropertyInputSelector GrassTypesAttribute;

	/** If toggled, will only generate grass types which are not selected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bExcludeSelectedGrassTypes = true;

	/** Skip CPU readback of emitted textures during initialization of the texture datas. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bSkipReadbackToCPU = false;

	/** Generate a landscape height texture. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bGenerateHeightMap = false;
};

struct FPCGGenerateLandscapeTexturesContext : public FPCGContext
{
public:
	~FPCGGenerateLandscapeTexturesContext();

	FLandscapeGrassWeightExporter* LandscapeGrassWeightExporter = nullptr;

	/** Output texture data objects. */
	TObjectPtr<UPCGTextureData> HeightTextureData;
	TArray<TObjectPtr<UPCGTextureData>> GrassMapTextureDatas;

	/** Exported result textures. */
	TRefCountPtr<IPooledRenderTarget> HeightHandle = nullptr;
	TRefCountPtr<IPooledRenderTarget> GrassMapHandle = nullptr;

	/** List of the grass types selected for generation. We have to hold their texture index as well, since
	 * this array could be sparse, but the actual texture array we produce will always have all the grass types.
	 */
	TArray<TTuple<TWeakObjectPtr<ULandscapeGrassType>, /*TextureIndex=*/int32>> SelectedGrassTypes;

	/** Total number of grass types used by the landscape component. Includes grass types which were not selected for generation. */
	int32 NumGrassTypes = 0;

	TWeakObjectPtr<ALandscapeProxy> LandscapeProxy = nullptr;
	TArray<TWeakObjectPtr<ULandscapeComponent>> LandscapeComponents;

	/** World-space bounds containing all of the landscape components given to the grass weight exporter. */
	FBox GrassMapBounds = FBox(EForceInit::ForceInit);

	/** Extent (side length) of each landscape component. */
	double LandscapeComponentExtent = 0.0;

	/** True when we have filtered all of the incoming landscape components down to the ones which overlap the given bounds. */
	bool bLandscapeComponentsFiltered = false;

	/** True when the landscape components are ready for rendering. */
	bool bReadyToRender = false;

	/** Textures that we wait to be streamed before generating the grass maps. */
	TArray<TObjectPtr<UTexture>> TexturesToStream;

	/** True when streaming has been requested on landscape textures. */
	bool bTextureStreamingRequested = false;

	/** True when grass map generation has been scheduled on the render thread. */
	bool bGenerationScheduled = false;

	double TexelSizeWorld = 0.0;

	TOptional<FTransform> OutputTextureTransform;

protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;
};

class FPCGGenerateLandscapeTexturesElement : public IPCGElement
{
public:
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;
	/** FLandscapeGrassWeightExporter expects to exist in scope only on the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const { return true; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
