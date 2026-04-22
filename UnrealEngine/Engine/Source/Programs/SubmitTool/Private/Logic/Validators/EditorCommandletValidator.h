// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ValidatorRunExecutable.h"

struct FEditorParameters
{
	FString EditorExePath;
	FString EditorArguments;
};

class FEditorCommandletValidator : public FValidatorRunExecutable
{

public:		
	FEditorCommandletValidator(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition);
	virtual void ParseDefinition(const FString& InDefinition) override;
	virtual bool Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags) override;
	virtual const FString& GetValidatorTypeName() const override { return SubmitToolParseConstants::UBTValidator; }
	virtual bool Activate() override;

protected:
	void GetEditorsForPaths(const TArray<FSourceControlStateRef>& InFilteredFilesInCL, TMap<FString, FEditorParameters>& OutProjectEditorParameters) const;
	void SortProjectsByFile(const TArray<FString>& InFiles, TSet<FString>& OutProjects, TSet<FString>& OutSubProjects) const;

	TMap<FString, TUniquePtr<FProcessWrapper>> Processes;
};
