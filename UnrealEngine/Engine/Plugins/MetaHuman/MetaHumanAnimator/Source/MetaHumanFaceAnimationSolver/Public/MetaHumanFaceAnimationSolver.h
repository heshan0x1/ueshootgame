// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetaHumanFaceAnimationSolver.generated.h"

#define UE_API METAHUMANFACEANIMATIONSOLVER_API


UENUM()
enum class EDepthMapInfluenceValue : uint8
{
	None = 0,
	Low,
	High,
};

UENUM()
enum class ETeethMode : uint8
{
	TrackingPoints = 0,
	Estimated,
};

/** MetaHuman Face Animation Solver
*
*   Holds configuration info used by the solver.
*
*/
UCLASS(MinimalAPI)
class UMetaHumanFaceAnimationSolver : public UObject
{
	GENERATED_BODY()

public:

	// Delegate called when something changes in the face animation solver that others should know about
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

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bOverrideDepthMapInfluence = false;

	/* The amount by which the depth-map is used to influence the solve result */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bOverrideDepthMapInfluence"))
	EDepthMapInfluenceValue DepthMapInfluence = EDepthMapInfluenceValue::High;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bOverrideEyeSolveSmoothness = false;

	/* The amount of smoothing to be applied to the eye gaze control results */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bOverrideEyeSolveSmoothness"))
	float EyeSolveSmoothness = 0.1;

	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (InlineEditConditionToggle))
	bool bOverrideTeethMode = false;

	/* Whether teeth tracking points are used or teeth position is estimated */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (EditCondition = "bOverrideTeethMode"))
	ETeethMode TeethMode = ETeethMode::TrackingPoints;

	UE_API bool CanProcess() const;
	UE_API bool SettingsOverridden() const;
	UE_API bool GetConfigDisplayName(class UCaptureData* InCaptureData, FString& OutName) const;

	UE_API FString GetSolverTemplateData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetSolverConfigData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetSolverDefinitionsData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetSolverHierarchicalDefinitionsData(class UCaptureData* InCaptureData = nullptr) const;
	UE_API FString GetSolverPCAFromDNAData(class UCaptureData* InCaptureData = nullptr) const;

	UE_API FOnInternalsChanged& OnInternalsChanged();

private:

	UE_API class UMetaHumanConfig* GetEffectiveConfig(class UCaptureData* InCaptureData) const;

	UE_API FString JsonObjectAsString(TSharedPtr<class FJsonObject> InJsonObject) const;

	FOnInternalsChanged OnInternalsChangedDelegate;

	UE_API void NotifyInternalsChanged();
};

#undef UE_API
