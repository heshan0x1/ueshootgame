// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "PCGSceneCapture.generated.h"

/** Perform a 2D orthographic scene capture and write the result to a render target data. Can be costly, use with caution with runtime generation. */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGSceneCaptureSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SceneCapture")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGSceneCaptureElement", "NodeTitle", "Scene Capture"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGSceneCaptureElement", "NodeTooltip", "Perform a 2D orthographic scene capture and write the result to a render target data. Can be costly, use with caution with runtime generation."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/** Subset of EPixelFormat exposed to UTextureRenderTarget2D. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TEnumAsByte<enum ETextureRenderTargetFormat> PixelFormat = ETextureRenderTargetFormat::RTF_RGBA16f;

	/** Specifies which component of the scene rendering should be output to the render target. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TEnumAsByte<enum ESceneCaptureSource> CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;

	/** Size of a texel in the render target in world units (cm). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(UIMin = "1.0", ClampMin = "1.0", PCG_Overridable))
	float TexelSize = 50.0f;

	/** Skip CPU readback during initialization of the render target data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bSkipReadbackToCPU = false;
};

struct FPCGSceneCaptureContext : public FPCGContext
{
protected:
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;

public:
	TObjectPtr<USceneCaptureComponent2D> SceneCaptureComponent = nullptr;
	TObjectPtr<UTextureRenderTarget2D> RenderTarget = nullptr;
	FTransform RenderTargetTransform = FTransform::Identity;
	bool bSubmittedSceneCapture = false;
};

class FPCGSceneCaptureElement : public IPCGElementWithCustomContext<FPCGSceneCaptureContext>
{
public:
	/** Required to create the scene capture component. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
