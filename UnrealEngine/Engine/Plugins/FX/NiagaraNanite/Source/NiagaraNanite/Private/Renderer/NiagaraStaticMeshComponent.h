// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

#include "Components/StaticMeshComponent.h"
#include "InstancedStaticMeshSceneProxyDesc.h"

#include "NiagaraStaticMeshComponent.generated.h"

class FRDGBuilder;
struct FGPUSceneWriteDelegateParams;

UCLASS()
class UNiagaraStaticMeshComponent : public UStaticMeshComponent
{
	GENERATED_UCLASS_BODY()

	using FCpuInstanceUpdateFunction = TFunction<void(FInstanceSceneDataBuffers::FWriteView&)>;
	using FGpuInstanceUpdateFunction = TFunction<void(FRDGBuilder&, const FGPUSceneWriteDelegateParams&)>;

public:
	//~ Begin UObject Interface
	virtual void OnComponentDestroyed(bool) override;
	//~ End UObject Interface

	//~ Begin UActorComponent Interface
	bool ShouldCreatePhysicsState() const override { return false; }
	bool IsHLODRelevant() const override { return false; }
	//~ End UActorComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FMatrix GetRenderMatrix() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& BoundTransform) const override;
	bool SupportsStaticLighting() const override { return false; }
#if WITH_EDITOR
	virtual FBox GetStreamingBounds() const override;
#endif
	virtual bool BuildTextureStreamingDataImpl(ETextureStreamingBuildType BuildType, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel, TSet<FGuid>& DependentResources, bool& bOutSupportsBuildTextureStreamingData) override;
	virtual void GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const override;
	//~ End UPrimitiveComponent Interface

	//~ Begin INavRelevantInterface Interface.
	bool IsNavigationRelevant() const override { return false; }
	//~ End INavRelevantInterface Interface.

	//~ Begin UStaticMeshComponentInterface
	virtual bool GetMaterialStreamingData(int32 MaterialIndex, FPrimitiveMaterialInfo& MaterialData) const override;
	virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite) override;
	//~ End UStaticMeshComponentInterface

	void GetSceneProxyDesc(FInstancedStaticMeshSceneProxyDesc& OutSceneProxyDesc) const;
	//void BuildSceneDesc(FPrimitiveSceneProxyDesc* InSceneProxyDesc, FPrimitiveSceneDesc& OutPrimitiveSceneDesc);

	void UpdateInstanceCPU(int32 NumRequiredInstances, FCpuInstanceUpdateFunction UpdateFunction);
	void UpdateInstanceGPU(int32 NumRequiredInstances, FGpuInstanceUpdateFunction UpdateFunction);

	bool				bUseCpuOnlyUpdates = false;
	int32				NumInstances = 0;
	int32				NumCustomDataFloats = 0;

	FCpuInstanceUpdateFunction PendingCpuUpdateFunction;
	//FGpuInstanceUpdateFunction PendingGpuUpdateFunction;
};
