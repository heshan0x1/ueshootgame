// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"
#include "DataTypes/PVData.h"
#include "PVFoliagePaletteSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVFoliagePaletteSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationFoliage")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor::Green; }

	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{EPVRenderType::FoliageGrid}; }
	virtual TArray<EPVRenderType> GetSupportedRenderTypes() const override { return TArray{EPVRenderType::FoliageGrid, EPVRenderType::Foliage, EPVRenderType::Mesh}; }
	
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, Category="Meshes", meta=(PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideFoliage = false;

	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>> FoliageMeshes;
	
	UPROPERTY(EditAnywhere, Category = "Meshes", EditFixedSize,
		Meta = (NoResetToDefault, EditCondition="bOverrideFoliage", AllowedClasses = "/Script/Engine.StaticMesh,/Script/Engine.SkeletalMesh", ShowOnlyInnerProperties, FullyExpand="true", Tooltip=
			"List of Foliage meshes used for this plant\n\nList of all foliage meshes applied based on procedural vegetation preset."))
	TArray<struct FPVFoliageInfo> FoliageInfos;
};

class FPVFoliageElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;

public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
};
