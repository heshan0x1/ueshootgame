// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetaHumanFaceFittingSolver.generated.h"

#define UE_API METAHUMANFACEFITTINGSOLVER_API



/** MetaHuman Face Fitting Solver
*
*   Holds configuration info used by the solver.
*
*/
UCLASS(MinimalAPI)
class UMetaHumanFaceFittingSolver : public UObject
{
	GENERATED_BODY()

public:

	// Delegate called when something changes in the face fitting solver data that others should know about
	DECLARE_MULTICAST_DELEGATE(FOnInternalsChanged)

#if WITH_EDITOR

	//~Begin UObject interface
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	UE_API virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
	//~End UObject interface

#endif

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bOverrideDeviceConfig = false;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bOverrideDeviceConfig"))
	TObjectPtr<class UMetaHumanConfig> DeviceConfig;

	UPROPERTY()
	TObjectPtr<class UMetaHumanFaceAnimationSolver> FaceAnimationSolver;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TObjectPtr<class UMetaHumanConfig> PredictiveSolver;

	/** Load the solvers for face fitting */
	UE_API void LoadFaceFittingSolvers();

	/** Load Solver that will be trained as part of preparing identity for performance */
	UE_API void LoadPredictiveSolver();

	UE_API bool CanProcess() const;
	UE_API bool GetConfigDisplayName(class UCaptureData* InCaptureData, FString& OutName) const;

	UE_API FString GetFittingTemplateData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetFittingConfigData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetFittingConfigTeethData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetFittingIdentityModelData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetFittingControlsData(class UCaptureData* InCaptureData = nullptr) const;

	UE_API TArray<uint8> GetPredictiveGlobalTeethTrainingData() const;
	UE_API TArray<uint8> GetPredictiveTrainingData() const;

	UE_API FOnInternalsChanged& OnInternalsChanged();

private:

	UE_API class UMetaHumanConfig* GetEffectiveConfig(class UCaptureData* InCaptureData) const;

	UE_API FString JsonObjectAsString(TSharedPtr<class FJsonObject> InJsonObject) const;

	FOnInternalsChanged OnInternalsChangedDelegate;

	UE_API void NotifyInternalsChanged();
};

#undef UE_API
