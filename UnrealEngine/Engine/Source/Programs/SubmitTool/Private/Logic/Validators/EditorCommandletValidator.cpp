// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorCommandletValidator.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"

#include "CommandLine/CmdLineParameters.h"

#include "Configuration/Configuration.h"
#include "Logging/SubmitToolLog.h"
#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"

#include "ProjectEditorRecords.h"


FEditorCommandletValidator::FEditorCommandletValidator(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition) :
	FValidatorRunExecutable(InNameId, InParameters, InServiceProvider, InDefinition)
{
	ParseDefinition(InDefinition);
}

void FEditorCommandletValidator::ParseDefinition(const FString& InDefinition)
{
	FStringOutputDevice Errors;
	Definition = MakeUnique<FEditorCommandletValidatorDefinition>();
	FEditorCommandletValidatorDefinition* ModifyableDefinition = const_cast<FEditorCommandletValidatorDefinition*>(GetTypedDefinition<FEditorCommandletValidatorDefinition>());
	FEditorCommandletValidatorDefinition::StaticStruct()->ImportText(*InDefinition, ModifyableDefinition, nullptr, 0, &Errors, FEditorCommandletValidatorDefinition::StaticStruct()->GetName());

	if(!Errors.IsEmpty())
	{
		UE_LOG(LogSubmitTool, Error, TEXT("[%s] Error loading parameter file %s"), *GetValidatorName(), *Errors);
		FModelInterface::SetErrorState();
	}
}

bool FEditorCommandletValidator::Activate()
{
	FEditorCommandletValidatorDefinition* ModifiableDefinition = const_cast<FEditorCommandletValidatorDefinition*>(GetTypedDefinition<FEditorCommandletValidatorDefinition>());
	ModifiableDefinition->EditorRecordsFile = FConfiguration::Substitute(ModifiableDefinition->EditorRecordsFile);
	ModifiableDefinition->bValidateExecutableExists = false;

	return FValidatorRunExecutable::Activate();
}

bool FEditorCommandletValidator::Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	TMap<FString, FEditorParameters> EditorParameters;
	GetEditorsForPaths(InFilteredFilesInCL, EditorParameters);

	bool bProcessesStarted = false;
	for (const TPair<FString, FEditorParameters>& EditorParametersSet : EditorParameters)
	{
		bProcessesStarted |= QueueProcess(EditorParametersSet.Key, EditorParametersSet.Value.EditorExePath, EditorParametersSet.Value.EditorArguments);
	}

	return bProcessesStarted;
}

void FEditorCommandletValidator::GetEditorsForPaths(const TArray<FSourceControlStateRef>& InFilteredFilesInCL, TMap<FString, FEditorParameters>& OutProjectEditorParameters) const
{
	const FEditorCommandletValidatorDefinition* TypedDefinition = GetTypedDefinition<FEditorCommandletValidatorDefinition>();

	TSharedPtr<FJsonObject> Projects;
	TSharedPtr<FJsonObject> SubProjects;

	if (!TypedDefinition->EditorRecordsFile.IsEmpty())
	{
		FProjectEditorRecord RecordsFile = FProjectEditorRecord::Load();

		const TSharedPtr<FJsonObject>* ProjectsPtr;
		if (RecordsFile.ProjectEditorJson->TryGetObjectField(FProjectEditorRecord::ProjectsProperty, ProjectsPtr))
		{
			Projects = *ProjectsPtr;
		}

		const TSharedPtr<FJsonObject>* SubProjectsPtr;
		if (RecordsFile.ProjectEditorJson->TryGetObjectField(FProjectEditorRecord::SubProjectProperty, SubProjectsPtr))
		{
			SubProjects = *SubProjectsPtr;
		}
	}

	TArray<FString> Filepaths;
	Algo::Transform(InFilteredFilesInCL, Filepaths, [](const FSourceControlStateRef& InFile) { return InFile->GetFilename(); });

	TSet<FString> ProjectSet;
	TSet<FString> SubProjectSet;
	SortProjectsByFile(Filepaths, ProjectSet, SubProjectSet);

	for (const FString& UProject : ProjectSet)
	{
		FEditorParameters EditorParams;     
		EditorParams.EditorArguments = FConfiguration::Substitute(TypedDefinition->ExecutableArguments).Replace(TEXT("$(ProjectName)"), *FPaths::GetBaseFilename(UProject));

		if (Projects && Projects->HasTypedField(UProject, EJson::Object))
		{
			EditorParams.EditorExePath = Projects->GetObjectField(UProject)->GetStringField(FProjectEditorRecord::EngineLocationProperty);
		}
		else
		{
			if (TypedDefinition->ExecutableCandidates.Num() != 0)
			{
				EditorParams.EditorExePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
			}
			else
			{
				EditorParams.EditorExePath = TypedDefinition->ExecutablePath;
			}
		}

		OutProjectEditorParameters.Add(FPaths::GetBaseFilename(UProject), MoveTemp(EditorParams));
	}

	for (const FString& SubProject : SubProjectSet)
	{
		FEditorParameters EditorParams;
		EditorParams.EditorArguments = FConfiguration::Substitute(TypedDefinition->ExecutableArguments).Replace(TEXT("$(ProjectName)"), *TypedDefinition->MainProject);

		if (SubProjects && SubProjects->HasTypedField(SubProject, EJson::Object))
		{
			const TSharedPtr<FJsonObject> SubProjectJson = SubProjects->GetObjectField(SubProject);

			if (SubProjectJson->HasTypedField(FProjectEditorRecord::EpicAppProperty, EJson::String))
			{
				EditorParams.EditorArguments.Append(TEXT(" -epicapp=") + SubProjectJson->GetStringField(FProjectEditorRecord::EpicAppProperty));
			}

			if (SubProjectJson->HasTypedField(FProjectEditorRecord::BaseDirProperty, EJson::String))
			{
				EditorParams.EditorArguments.Append(TEXT(" -BaseDir=") + SubProjectJson->GetStringField(FProjectEditorRecord::BaseDirProperty));
			}

			EditorParams.EditorExePath = SubProjectJson->GetStringField(FProjectEditorRecord::EngineLocationProperty);
		}
		else
		{
			if (TypedDefinition->ExecutableCandidates.Num() != 0)
			{
				EditorParams.EditorExePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
			}
			else
			{
				EditorParams.EditorExePath = TypedDefinition->ExecutablePath;
			}
		}


		EditorParams.EditorArguments.Append(TEXT(" -ValkyrieProject=") + SubProject);

		OutProjectEditorParameters.Add(FPaths::GetBaseFilename(SubProject), MoveTemp(EditorParams));
	}
}


void FEditorCommandletValidator::SortProjectsByFile(const TArray<FString>& InFiles, TSet<FString>& OutProjects, TSet<FString>& OutSubProjects) const
{
	TSet<FString> CheckedDirectories;
	TSet<FString> ProjectFiles;

	for (const FString& File : InFiles)
	{		
		FString CurrentDir = FPaths::GetPath(File);

		while (!CurrentDir.IsEmpty())
		{
			bool bIsAlreadyInSet = false;
			CheckedDirectories.Add(CurrentDir, &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				break;
			}

			TArray<FString> Projects;
			TArray<FString> SubProjects;
			IFileManager::Get().FindFiles(SubProjects, *(CurrentDir / TEXT("*.uefnproject")), true, false);
			IFileManager::Get().FindFiles(Projects, *(CurrentDir / TEXT("*.uproject")), true, false);

			if (SubProjects.Num() > 0)
			{
				OutSubProjects.Add(CurrentDir + "/" + SubProjects[0]);
				break;
			}
			else if (Projects.Num() > 0)
			{
				OutProjects.Add(CurrentDir + "/" + Projects[0]);
				break;
			}
			else
			{
				CurrentDir = FPaths::GetPath(CurrentDir);
			}
		}
	}
}