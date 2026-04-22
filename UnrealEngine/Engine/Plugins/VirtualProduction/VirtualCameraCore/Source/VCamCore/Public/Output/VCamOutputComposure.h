// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "Engine/TextureRenderTarget2D.h"

class AActor;

#include "VCamOutputComposure.generated.h"

UCLASS()
class UE_DEPRECATED(5.7, "Composure output provider has been deprecated and will be removed.") VCAMCORE_API UVCamOutputComposure : public UVCamOutputProviderBase
{
	GENERATED_BODY()
public:
	
	/** List of Composure stack Compositing Elements to render the requested UMG into */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	TArray<TSoftObjectPtr<AActor>> LayerTargets;

	/** TextureRenderTarget2D asset that contains the final output */
	UE_DEPRECATED(5.7, "UVCamOutputProviderBase now provides a final output render target.")
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> FinalOutputRenderTarget_DEPRECATED = nullptr;

	UVCamOutputComposure();

protected:

};
