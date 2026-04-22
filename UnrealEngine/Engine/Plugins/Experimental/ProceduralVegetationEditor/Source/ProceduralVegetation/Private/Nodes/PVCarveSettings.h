// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"
#include "Implementations/PVCarve.h"
#include "DataTypes/PVData.h"
#include "PVCarveSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVCarveSettings : public UPVBaseSettings
{
	GENERATED_BODY()

	friend class FPVCarveElement;

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationCarve")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor::Yellow; }

	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{EPVRenderType::PointData}; }
#endif

protected:
	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;

	virtual FPCGElementPtr CreateElement() const override;

private:
	UPROPERTY(EditAnywhere, Category = "Carve", meta = (ShowOnlyInnerProperties))
	FPVCarveParams CarveSettings;
};

class FPVCarveElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
