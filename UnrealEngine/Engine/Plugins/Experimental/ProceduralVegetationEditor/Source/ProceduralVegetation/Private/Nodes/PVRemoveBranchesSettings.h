// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"
#include "Implementations/PVRemoveBranches.h"
#include "PVRemoveBranchesSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVRemoveBranchesSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationRemoveBranches")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor::Yellow; }

	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{ EPVRenderType::PointData }; }
#endif

protected:
	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;

	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, Category = Settings, meta=(Tooltip="Sets the rule used to remove branches.\n\nChooses the basis for pruning (e.g., generation, length from root, age, or radius). Determines which branches are candidates for removal."))
	ERemoveBranchesBasis BranchRemoveBasis = ERemoveBranchesBasis::Length;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(ClampMin = 0f, ClampMax = 1.0f, UIMin = 0f, UIMax = 1.0f, Tooltip="Removal threshold for the chosen basis.\n\nBranches exceeding or falling below this threshold (per the selected basis) are removed. Use lower/higher values to prune sparsely or aggressively."))
	float Threshold = 0;
};

class FPVRemoveBranchesElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
