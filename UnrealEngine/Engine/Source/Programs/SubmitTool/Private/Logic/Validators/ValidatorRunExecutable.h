// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ValidatorBase.h"

class FBuildVersion;

struct FProcessTrackingData
{
	bool bIgnoringOutputError;
	TArray<FString> ErrorList;
};

/**
 * @brief Runs an executable with optional arguments. Return code 0 on the executable means success.
 * Validator values: "ExecutablePath" (required), "ExecutableArguments" (optional)
 */
class FValidatorRunExecutable : public FValidatorBase
{
public:	
	FValidatorRunExecutable(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition);
	virtual void ParseDefinition(const FString& InDefinition) override;

	virtual void StartValidation() override;
	virtual bool Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags) override;
	virtual const FString& GetValidatorTypeName() const override { return SubmitToolParseConstants::CustomValidator; }
	virtual bool Activate() override;

	virtual void StopInternalValidations() override;
	 
protected:
	virtual bool QueueProcess(const FString& ProcessId, const FString& LocalPath, const FString& Args);
	virtual void OnProcessOutputLine(const FString& ProcessId, const FString& Line, const EProcessOutputType& OutputType);
	virtual bool IsLineAnError(const FString& InLine, bool& InOutbIgnoringOutputErrors);
	virtual void OnProcessComplete(const FString& ProcessId, int32 ReturnCode);
	
	virtual void Tick(float DeltaTime) override;

	virtual const TArray<FAnalyticsEventAttribute> GetTelemetryAttributes() const override;

	bool DoesExecutableNeedBuilding() const;
	bool FindBuildVersionForExecutable(const FString& ExecutablePath, FBuildVersion& OutBuildVersion) const;

	void PrepareExecutableOptions();


protected:
	const FString ExecutableOptions = TEXT("SelectedExecutable");
	TMap<FString, FProcessTrackingData> ProcessesData;
	TArray<FProcessWrapper> Processes;
};
