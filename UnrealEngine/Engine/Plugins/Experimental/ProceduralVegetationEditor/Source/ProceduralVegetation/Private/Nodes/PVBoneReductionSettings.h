// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"
#include "DataTypes/PVData.h"
#include "PVBoneReductionSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVBoneReductionSettings : public UPVBaseSettings
{
	GENERATED_BODY()

	friend class FPVBoneReductionElement;

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationBoneReduction")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(1.0f, 0.0f, 0.599f); }

	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{EPVRenderType::Bones, EPVRenderType::Mesh}; }
#endif

protected:
	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;

	virtual FPCGElementPtr CreateElement() const override;

private:
	UPROPERTY(EditAnywhere, Category = "Bone Reduction", meta = (ShowOnlyInnerProperties, ClampMin ="0", ClampMax = "1", UIMin = "0", UIMax = "1", Tooltip="Controls how aggressively bones are reduced.\n\nControls how aggressively bones are reduced. Higher values result in less bones. Reduced bone complexity helps in optimizing performance but can effect the accuracy of wind simulation."))
	float Strength;
};

class FPVBoneReductionElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
