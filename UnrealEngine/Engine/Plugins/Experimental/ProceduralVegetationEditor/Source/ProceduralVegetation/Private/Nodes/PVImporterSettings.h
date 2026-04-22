// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Nodes/PVBaseSettings.h"

#include "PVImporterSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVImporterSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
	UPVImporterSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ProceduralVegetationImporter")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PVImporterSettings", "NodeTitle", "Procedural Vegetation Importer"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PVImporterSettings", "NodeTooltip", "Create Procedural Vegetation Skeleton"); }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(FColor::Black); }

	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{ EPVRenderType::PointData }; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, Category="Skeleton", meta=(RelativeToGameDir))
	FFilePath SkeletonFile;
};

class FPVImporterElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
