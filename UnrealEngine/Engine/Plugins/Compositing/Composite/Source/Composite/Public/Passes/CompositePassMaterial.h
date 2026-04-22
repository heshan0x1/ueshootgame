// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositePassBase.h"

#include "CompositePassMaterial.generated.h"

#define UE_API COMPOSITE_API

class UMaterialInterface;

/** Post-process material holdout composite pass. */
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "Post-Process Material Pass"))
class UCompositePassMaterial : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassMaterial(const FObjectInitializer& ObjectInitializer);

	/** Destructor */
	UE_API ~UCompositePassMaterial();

	UE_API virtual bool IsActive() const override;

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

	UE_API virtual bool NeedsSceneTextures() const override;

public:
	/** Post-process material to execute. Input0 is connected to SceneTexture's PostprocessInput0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	TObjectPtr<UMaterialInterface> PostProcessMaterial;
};

#undef UE_API
