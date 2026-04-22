// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "DataTypes/PVData.h"
#include "Helpers/PVUtilities.h"
#include "PVBaseSettings.generated.h"

UCLASS(BlueprintType, Abstract, HideCategories=(Debug), ClassGroup = (Procedural))
class UPVBaseSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPVBaseSettings();
	
#if WITH_EDITOR
	bool bLockInspection = false;
	
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
	virtual TArray<EPVRenderType> GetDefaultRenderType() const { return TArray<EPVRenderType>(); }
	virtual TArray<EPVRenderType> GetSupportedRenderTypes() const { return GetDefaultRenderType(); }
	virtual TArray<EPVRenderType> GetCurrentRenderTypes() const { return CurrentRenderType; }
	virtual void SetCurrentRenderType(TArray<EPVRenderType> InRenderTypes);
	
	FPVDebugSettings GetDebugVisualizationSettings() const { return DebugVisualizationSettings; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	virtual bool HasExecutionDependencyPin() const override { return false; }

	virtual FPCGDataTypeIdentifier GetInputPinTypeIdentifier() const;
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
private:
	
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = DebugSettings, meta=(EditCondition = "bDebug", EditConditionHides))
	FPVDebugSettings DebugVisualizationSettings;

	UPROPERTY()
	TArray<EPVRenderType> CurrentRenderType;
#endif
};

class FPVBaseElement : public IPCGElement
{
protected:
	virtual void PostExecuteInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};