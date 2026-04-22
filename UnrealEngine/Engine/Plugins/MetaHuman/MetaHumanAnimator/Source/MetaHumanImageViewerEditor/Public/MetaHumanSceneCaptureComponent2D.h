// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "MetaHumanSceneCaptureComponent2D.generated.h"

#define UE_API METAHUMANIMAGEVIEWEREDITOR_API

class FEditorViewportClient;

UCLASS(MinimalAPI)
class UMetaHumanSceneCaptureComponent2D : public USceneCaptureComponent2D
{
public:
	GENERATED_BODY()

	UE_API UMetaHumanSceneCaptureComponent2D(const FObjectInitializer& InObjectInitializer);

	//~ USceneCaptureComponent2D interface
	UE_API virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:

	// Sets the viewport client that controls this component
	UE_API void SetViewportClient(TWeakPtr<class FEditorViewportClient> InPerformerViewportClient);

	/** Set the ShowFlags for this component based a view mode index */
	UE_API void SetViewMode(EViewModeIndex InViewMode);

	UE_API void InvalidateCache();

private:

	// A reference to the viewport client that controls this component
	TWeakPtr<FEditorViewportClient> ViewportClientRef;

	static constexpr int32 NumTicksAfterCacheInvalidation = 2;
	int32 CurrentNumTicksAfterCacheInvalidation = 0;
	float CachedFOVAngle = -1;
	float CachedCustomNearClippingPlane = -1;
	FRotator CachedViewRotation = FRotator(0, 0, 0);
	FVector CachedViewLocation = FVector(0, 0, 0);
};

#undef UE_API
