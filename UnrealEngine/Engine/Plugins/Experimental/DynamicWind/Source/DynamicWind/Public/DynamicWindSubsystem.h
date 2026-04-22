// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SpanAllocator.h"
#include "Matrix3x4.h"
#include "Containers/Map.h"
#include "DynamicWindLog.h"
#include "DynamicWindParameters.h"
#include "DynamicWindSubsystem.generated.h"

class USkeleton;
class UInstancedSkinnedMeshComponent;
class FDynamicWindTransformProvider;
namespace Nanite { class FSkinnedSceneProxy; }

UCLASS(BlueprintType, ClassGroup = (Rendering, Common))
class DYNAMICWIND_API UDynamicWindSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void PostInitialize() override;
	virtual void PreDeinitialize() override;

	UE_DEPRECATED(5.7, "This method no longer has any effect. It shouldn't be used externally and will be removed next release")
	void RegisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy);
	UE_DEPRECATED(5.7, "This method no longer has any effect. It shouldn't be used externally and will be removed next release")
	void UnregisterSceneProxy(const Nanite::FSkinnedSceneProxy* InSkinnedProxy);

public:
	UFUNCTION(BlueprintCallable, Category = "DynamicWind")
	float GetBlendedWindAmplitude() const;

	UFUNCTION(BlueprintCallable, Category = "DynamicWind")
	void UpdateWindParameters(const FDynamicWindParameters& Parameters);

private:
	using FTransformProviderPtr = TSharedPtr<FDynamicWindTransformProvider, ESPMode::ThreadSafe>;

	FTransformProviderPtr TransformProvider;
};
