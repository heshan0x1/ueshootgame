// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VPFullScreenUserWidget_PostProcessBase.h"
#include "VPFullScreenUserWidget_PostProcess.generated.h"

class AActor;
class UPostProcessComponent;

/**
 * Renders widget by adding it as a blend material.
 */
USTRUCT()
struct FVPFullScreenUserWidget_PostProcess : public FVPFullScreenUserWidget_PostProcessBase
{
	GENERATED_BODY()

	PRAGMA_DISABLE_DEPRECATION_WARNINGS

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.7, "Composure layers are no longer supported on the VP Full screen user widget.")		
	UPROPERTY()
	TArray<TObjectPtr<AActor>> ComposureLayerTargets;
#endif

	FVPFullScreenUserWidget_PostProcess() = default;
	FVPFullScreenUserWidget_PostProcess(const FVPFullScreenUserWidget_PostProcess&) = default;
	FVPFullScreenUserWidget_PostProcess(FVPFullScreenUserWidget_PostProcess&&) = default;
	FVPFullScreenUserWidget_PostProcess& operator=(const FVPFullScreenUserWidget_PostProcess&) = default;
	FVPFullScreenUserWidget_PostProcess& operator=(FVPFullScreenUserWidget_PostProcess&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	void SetCustomPostProcessSettingsSource(TWeakObjectPtr<UObject> InCustomPostProcessSettingsSource);

	bool Display(UWorld* World, UUserWidget* Widget, TAttribute<float> InDPIScale);
	
	UE_DEPRECATED(5.7, "bInRenderToTextureOnly parameter is deprecated and use method call without that parameter.")
	bool Display(UWorld* World, UUserWidget* Widget, bool bInRenderToTextureOnly, TAttribute<float> InDPIScale)
	{
		return Display(World, Widget, InDPIScale);
	}
	
	virtual void Hide(UWorld* World) override;
	void Tick(UWorld* World, float DeltaSeconds);

private:
	
	/** Post process component used to add the material to the post process chain. */
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> PostProcessComponent = nullptr;
	
	/** The dynamic instance of the material that the render target is attached to. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostProcessMaterialInstance;

	/**
	 * Optional. Some object that contains a FPostProcessSettings property. These settings will be used for PostProcessMaterialInstance.
	 * E.g. VCam uses this to use post process from a specific cine camera.
	 */
	TWeakObjectPtr<UObject> CustomPostProcessSettingsSource;
	
	bool InitPostProcessComponent(UWorld* World);
	bool UpdateTargetPostProcessSettingsWithMaterial();
	void ReleasePostProcessComponent();
	
	virtual bool OnRenderTargetInited() override;

	FPostProcessSettings* GetPostProcessSettings() const;
};
