// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"

#include "PVPresetLoaderSettings.generated.h"

struct FPVPresetVariationInfo;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVPresetLoaderSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationPresetLoader")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{ EPVRenderType::PointData }; }

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, Category="Preset", meta=(Tooltip="Loads the selected procedural vegetation preset\n\nThis allows the user to load a procedural vegetation preset. A procedural vegatation preset contains data for a specie of vegetaion. It contains hormonal information, the main structure of the vegetation, data about the branches, where foliage will/can be attached, references to foliage meshes, materials to be applied"))
	TObjectPtr<class UProceduralVegetationPreset> Preset = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Preset")
	TArray<FPVPresetVariationInfo> PresetVariations;

private:
	void FillPresetVariationsInfo();
};

class FPVPresetLoaderElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
