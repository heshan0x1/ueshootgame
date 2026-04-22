// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Implementations/PVMeshBuilder.h"
#include "Implementations/PVMaterialSettings.h"
#include "Nodes/PVBaseSettings.h"
#include "PVMeshBuilderSettings.generated.h"

class UMaterialInterface;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVMeshBuilderSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationMesher")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(FColor::Cyan); }
	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{EPVRenderType::Mesh}; }
	virtual TArray<EPVRenderType> GetSupportedRenderTypes() const override { return TArray{EPVRenderType::PointData, EPVRenderType::Mesh}; }

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	virtual void PostLoad() override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;

	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;

public:	
	UPROPERTY(EditAnywhere, Category = "Mesh", meta = (ShowOnlyInnerProperties))
	FPVMeshBuilderParams MesherSettings;

	UPROPERTY()
	FPVMaterialSettings CustomMaterialSettings;

	UPROPERTY()
	FPVMaterialSettings DefaultMaterialSettings;

	UPROPERTY()
	bool bCustomMaterialSet = false;

	UPROPERTY()
	FString DisplacementWarnings;
	
	UFUNCTION()
	TArray<FString> GetPlantProfileOptions() const
	{
		return MesherSettings.PlantProfileOptions;
	}
};

class FPVMeshBuilderElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
