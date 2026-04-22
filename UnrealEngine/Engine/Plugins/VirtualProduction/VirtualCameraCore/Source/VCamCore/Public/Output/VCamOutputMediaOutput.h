// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VCamOutputProviderBase.h"
#include "MediaCapture.h"
#include "MediaOutput.h"

#if WITH_EDITOR
#endif

#include "VCamOutputMediaOutput.generated.h"

UCLASS(meta = (DisplayName = "Media Output Provider"))
class VCAMCORE_API UVCamOutputMediaOutput: public UVCamOutputProviderBase
{
	GENERATED_BODY()
public:
	
	/** Media Output configuration asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	TObjectPtr<UMediaOutput> OutputConfig;

	UE_DEPRECATED(5.7, "Using composure as an output provider is no longer supported. You can specify a final render target directly on the output provider.")
	UPROPERTY()
	int32 FromComposureOutputProviderIndex_DEPRECATED = INDEX_NONE;

	UVCamOutputMediaOutput();
	
	//~ Begin UVCamOutputProviderBase Interface
	virtual void OnActivate() override;
	virtual void OnDeactivate() override;
	//~ End UVCamOutputProviderBase Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	
	UPROPERTY(Transient)
	TObjectPtr<UMediaCapture> MediaCapture = nullptr;

private:
	
	void StartCapturing();
	void StopCapturing();
};
