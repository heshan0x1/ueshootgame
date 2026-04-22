// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CompositePassBase.h"
#include "Passes/CompositeCorePassMergeProxy.h"

#include "OpenColorIOColorSpace.h"

#include "CompositePassOpenColorIO.generated.h"

#define UE_API COMPOSITE_API

/** Pass to apply an OpenColorIO transform. */
UCLASS(MinimalAPI, BlueprintType, Blueprintable, EditInlineNew, CollapseCategories, meta = (DisplayName = "OpenColorIO Pass"))
class UCompositePassOpenColorIO : public UCompositePassBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UE_API UCompositePassOpenColorIO(const FObjectInitializer& ObjectInitializer);
	/** Destructor */
	UE_API ~UCompositePassOpenColorIO();

	UE_API virtual bool GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const override;

public:
	/** Color conversion settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	FOpenColorIOColorConversionSettings ColorConversionSettings;
};

#undef UE_API
