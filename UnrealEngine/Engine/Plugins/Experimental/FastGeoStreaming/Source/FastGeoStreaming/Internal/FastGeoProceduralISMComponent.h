// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FastGeoStaticMeshComponent.h"
#include "InstancedStaticMeshSceneProxyDesc.h"

struct FProceduralISMComponentDescriptor;

class FFastGeoProceduralISMComponent : public FFastGeoStaticMeshComponentBase
{
public:
	typedef FFastGeoStaticMeshComponentBase Super;

	/** Static type identifier for this element class */
	FASTGEOSTREAMING_API static const FFastGeoElementType Type;

	FFastGeoProceduralISMComponent(int32 InComponentIndex = INDEX_NONE, FFastGeoElementType InType = Type);
	virtual ~FFastGeoProceduralISMComponent() = default;

	FASTGEOSTREAMING_API void InitializeFromComponentDescriptor(const FProceduralISMComponentDescriptor& InDescriptor);

	//~ Begin FFastGeoComponent Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End FFastGeoComponent Interface

protected:
	//~ Begin FFastGeoPrimitiveComponent interface
	virtual FPrimitiveSceneProxyDesc& GetSceneProxyDesc() override { return SceneProxyDesc; }
	virtual const FPrimitiveSceneProxyDesc& GetSceneProxyDesc() const override { return SceneProxyDesc; }
	virtual bool IsNavigationRelevant() const override { return false; }
#if WITH_EDITOR
	virtual void ResetSceneProxyDescUnsupportedProperties() override;
#endif
	virtual void InitializeSceneProxyDescDynamicProperties() override;
	virtual void ApplyWorldTransform(const FTransform& InTransform) override;
	//~ End FFastGeoPrimitiveComponent interface

	//~ Begin FFastGeoStaticMeshComponentBase interface
	virtual FStaticMeshSceneProxyDesc& GetStaticMeshSceneProxyDesc() override { return SceneProxyDesc; }
	virtual const FStaticMeshSceneProxyDesc& GetStaticMeshSceneProxyDesc() const override { return SceneProxyDesc; }
	virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(const Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite) override;
#if !WITH_EDITOR && UE_WITH_PSO_PRECACHING
	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FMaterialInterfacePSOPrecacheParamsList& OutParams) override;
#endif
	//~ End FFastGeoStaticMeshComponentBase interface

	TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& BuildInstanceData();

private:
	// Persistent Data
	int32 NumInstances = 0;
	int32 NumCustomDataFloats = 0;
	FBox PrimitiveBoundsOverride = FBox(ForceInit);
	FInstancedStaticMeshSceneProxyDesc SceneProxyDesc{};

	// Transient data
	TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> DataProxy{};

	friend class FInstancedStaticMeshComponentHelper;
};
