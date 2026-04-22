// Copyright Epic Games, Inc. All Rights Reserved.


#include "ValidatorRunExecutable.h"

#include "AnalyticsEventAttribute.h"
#include "CommandLine/CmdLineParameters.h"
#include "Configuration/Configuration.h"
#include "HAL/FileManagerGeneric.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"
#include "Modules/BuildVersion.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FValidatorRunExecutable::FValidatorRunExecutable(const FName& InNameId, const FSubmitToolParameters& InParameters, TSharedRef<FSubmitToolServiceProvider> InServiceProvider, const FString& InDefinition) :
	FValidatorBase(InNameId, InParameters, InServiceProvider, InDefinition)
{
	ParseDefinition(InDefinition);
}

void FValidatorRunExecutable::ParseDefinition(const FString& InDefinition)
{
	FStringOutputDevice Errors;
	Definition = MakeUnique<FValidatorRunExecutableDefinition>();
	FValidatorRunExecutableDefinition* ModifyableDefinition = const_cast<FValidatorRunExecutableDefinition*>(GetTypedDefinition<FValidatorRunExecutableDefinition>());
	FValidatorRunExecutableDefinition::StaticStruct()->ImportText(*InDefinition, ModifyableDefinition, nullptr, 0, &Errors, FValidatorRunExecutableDefinition::StaticStruct()->GetName());

	if(!Errors.IsEmpty())
	{
		UE_LOG(LogSubmitTool, Error, TEXT("Error loading parameter file %s"), *Errors);
		FModelInterface::SetErrorState();
	}
}

void FValidatorRunExecutable::StartValidation()
{
	Processes.Reset();
	FValidatorBase::StartValidation();
}

bool FValidatorRunExecutable::Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	FString FinalArgs{ FConfiguration::Substitute(TypedDefinition->ExecutableArguments) };
	FinalArgs.ReplaceInline(TEXT("$(TaskName)"), *GetValidatorName());

	TArray<FString> Files;

	// aggregate all the valid files
	for(const FSourceControlStateRef& File : InFilteredFilesInCL)
	{
		Files.Add(File.Get().GetFilename());
	}

	if(!bIsValidSetup)
	{
		LogFailure(FString::Printf(TEXT("[%s] This task is not correctly setup and it's required for this change"), *ValidatorName));
		return false;
	}

	// create a file with the files and pass it as an argument of the validator (better to avoid breaking command line length limits)
	if(!TypedDefinition->FileListArgument.IsEmpty())
	{
		FString ValidatorDirectory = FPaths::EngineDir() + TEXT("Intermediate/SubmitTool/FileLists/");

		FGuid guid = FGuid::NewGuid();
		FString FileListPath = FPaths::ConvertRelativePathToFull(ValidatorDirectory + guid.ToString(EGuidFormats::DigitsWithHyphens) + TEXT(".txt"));

		FFileHelper::SaveStringArrayToFile(Files, *FileListPath, FFileHelper::EEncodingOptions::ForceAnsi, &IFileManager::Get(), EFileWrite::FILEWRITE_None);

		FinalArgs += FString::Printf(TEXT(" %s\"%s\""), *TypedDefinition->FileListArgument, *FileListPath);
	}

	// Legacy, pass down each file to the validator in the command line
	else if(!TypedDefinition->FileInCLArgument.IsEmpty())
	{
		for(const FString& File : Files)
		{
			FinalArgs += " " + TypedDefinition->FileInCLArgument + File;
		}
	}

	FString ExecutablePath;
	if(TypedDefinition->ExecutableCandidates.Num() != 0)
	{
		ExecutablePath = OptionsProvider.GetSelectedOptionValue(ExecutableOptions);
	}
	else
	{
		ExecutablePath = TypedDefinition->ExecutablePath;
	}

	return QueueProcess(TEXT("Process #1"), ExecutablePath, FinalArgs);
}

bool FValidatorRunExecutable::Activate()
{
	bIsValidSetup = FValidatorBase::Activate();

	PrepareExecutableOptions();
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	FString SelectedOption = OptionsProvider.GetSelectedOptionKey(ExecutableOptions);
	if(TypedDefinition->ExecutableCandidates.Num() > 0 && SelectedOption.IsEmpty())
	{
		TArray<FString> ExecutablePaths;
		TypedDefinition->ExecutableCandidates.GenerateValueArray(ExecutablePaths);
		ActivationErrors.Add(FString::Printf(TEXT("[%s] None of the executable candidates exists locally:\n%s"), *ValidatorName, *FString::Join(ExecutablePaths, TEXT("\n"))));
		bIsValidSetup = false;
	}

	FValidatorRunExecutableDefinition* ModifiableDefinition = const_cast<FValidatorRunExecutableDefinition*>(TypedDefinition);

	ModifiableDefinition->ExecutablePath = FConfiguration::SubstituteAndNormalizeFilename(TypedDefinition->ExecutablePath);
	ModifiableDefinition->BuiltRegexError = MakeShared<FRegexPattern>(ModifiableDefinition->RegexErrorParsing, ERegexPatternFlags::CaseInsensitive);
	ModifiableDefinition->BuiltRegexWarning = MakeShared<FRegexPattern>(ModifiableDefinition->RegexWarningParsing, ERegexPatternFlags::CaseInsensitive);

	static TArray<FString> ValidExtensions =
	{
#if PLATFORM_WINDOWS
		TEXT(".exe"),
		TEXT(".bat"),
#elif PLATFORM_MAC
		TEXT(".app"),
		TEXT(".sh"),
		TEXT(".command"),
		TEXT(""),
#elif PLATFORM_LINUX
		TEXT(".sh"),
		TEXT(""),
#endif
	};

	if(TypedDefinition->ExecutablePath.IsEmpty())
	{
		for(const TTuple<FString,FString>& Paths : TypedDefinition->ExecutableCandidates)
		{
			if(!ValidExtensions.Contains(FPaths::GetExtension(Paths.Value, true)))
			{
				ActivationErrors.Add(FString::Printf(TEXT("Task '%s' executable has an invalid extension for this platform: %s"), *ValidatorName, *Paths.Value));
				bIsValidSetup = false;
			}
		}

		if(TypedDefinition->ExecutableCandidates.Num() == 0)
		{
			ActivationErrors.Add(FString::Printf(TEXT("Task '%s' does not have a value for 'ExecutablePath' or 'ExecutableCandidates'."), *ValidatorName));
			bIsValidSetup = false;
		}
	}
	else
	{
		if(!ValidExtensions.Contains(FPaths::GetExtension(TypedDefinition->ExecutablePath, true)))
		{
			ActivationErrors.Add(FString::Printf(TEXT("Task '%s' executable has an invalid extension for this platform: %s"), *ValidatorName, *TypedDefinition->ExecutablePath));
			bIsValidSetup = false;
		}

		if(TypedDefinition->bValidateExecutableExists && !FPaths::FileExists(TypedDefinition->ExecutablePath))
		{
			ActivationErrors.Add(FString::Printf(TEXT("Task '%s' executable is not found on disk: %s."), *ValidatorName, *TypedDefinition->ExecutablePath));
			bIsValidSetup = false;
		}
	}

	if(!TypedDefinition->ExecutablePath.IsEmpty() && TypedDefinition->ExecutableCandidates.Num() > 0)
	{
		ActivationErrors.Add(FString::Printf(TEXT("Specifying ExecutablePath and ExecutableCandidates for task %s is not supported, please check your config."), *GetValidatorName()));
	}


	return bIsValidSetup;
}

void FValidatorRunExecutable::StopInternalValidations()
{
	if(GetValidatorState() == EValidationStates::Running)
	{
		for (FProcessWrapper& ProcessWrapper : Processes)
		{
			ProcessWrapper.Stop();
		}
	}
}

bool FValidatorRunExecutable::QueueProcess(const FString& InId, const FString& LocalPath, const FString& Args)
{
	if (!IFileManager::Get().FileExists(*LocalPath))
	{
		LogFailure(FString::Printf(TEXT("[%s (%s)] Process executable doesn't exist: %s"), *GetValidatorName(), *InId, *LocalPath));
		return false;
	}
	
	FProcessTrackingData ProcessData;
	ProcessData.bIgnoringOutputError = !GetTypedDefinition<FValidatorRunExecutableDefinition>()->EnableOutputErrorsAnchor.IsEmpty();
	ProcessesData.FindOrAdd(InId) = ProcessData;

	FOnOutputLine LineHandler = FOnOutputLine::CreateLambda([this, InId](const FString& InOutput, const EProcessOutputType& InType)
	{
		OnProcessOutputLine(InId, InOutput, InType);
	});

	FOnCompleted CompleteHandler = FOnCompleted::CreateLambda([this, InId](const int& InResult)
	{
		OnProcessComplete(InId, InResult);
	});
	Processes.Emplace(InId, LocalPath, Args, MoveTemp(CompleteHandler), MoveTemp(LineHandler), FConfiguration::Substitute(TEXT("$(root)")), GetTypedDefinition<FValidatorRunExecutableDefinition>()->bLaunchHidden, GetTypedDefinition<FValidatorRunExecutableDefinition>()->bLaunchReallyHidden);
	return true;
}

void FValidatorRunExecutable::OnProcessOutputLine(const FString& ProcessId, const FString& Line, const EProcessOutputType& OutputType)
{
	if(OutputType == EProcessOutputType::ProcessError || IsLineAnError(Line, ProcessesData[ProcessId].bIgnoringOutputError))
	{
		const FString& FormattedMessage = FString::Printf(TEXT("[%s (%s)]: %s"), *GetValidatorName(), *ProcessId, *Line);
		LogFailure(FormattedMessage);
		ProcessesData[ProcessId].ErrorList.Add(FormattedMessage);
	}
	else
	{
		UE_LOG(LogValidators, Log, TEXT("[%s (%s)]: %s"), *GetValidatorName(), *ProcessId, *Line);
	}
}

bool FValidatorRunExecutable::IsLineAnError(const FString& InLine, bool& InOutbIgnoringOutputErrors)
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();

	if(!TypedDefinition->EnableOutputErrorsAnchor.IsEmpty() && InLine.Find(TypedDefinition->EnableOutputErrorsAnchor, ESearchCase::IgnoreCase) != INDEX_NONE)
	{
		InOutbIgnoringOutputErrors = false;
		return false;
	}
	
	if(!TypedDefinition->DisableOutputErrorsAnchor.IsEmpty() && InLine.Find(TypedDefinition->DisableOutputErrorsAnchor, ESearchCase::IgnoreCase) != INDEX_NONE)
	{
		InOutbIgnoringOutputErrors = true;
		return false;
	}

	if(InOutbIgnoringOutputErrors)
	{
		return false;
	}

	for(const FString& Message : TypedDefinition->IgnoredErrorMessages)
	{
		if(InLine.Contains(Message, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	for(const FString& Message : TypedDefinition->ErrorMessages)
	{
		if(InLine.Contains(Message, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	{

		FRegexMatcher Regex = FRegexMatcher(*TypedDefinition->BuiltRegexError, InLine);
		if(Regex.FindNext())
		{
			return true;
		}
	}

	if(Definition->bTreatWarningsAsErrors)
	{
		FRegexMatcher Regex = FRegexMatcher(*TypedDefinition->BuiltRegexWarning, InLine);
		if(Regex.FindNext())
		{
			return true;
		}
	}

	return false;
}

void FValidatorRunExecutable::OnProcessComplete(const FString& ProcessId, int32 ReturnCode)
{
	const FValidatorRunExecutableDefinition* Def = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	const bool Success = Def->AllowedExitCodes.Contains(ReturnCode) && (Def->bOnlyLookAtExitCode || ProcessesData[ProcessId].ErrorList.IsEmpty());

	if(Success)
	{
		UE_LOG(LogValidators, Log, TEXT("[%s (%s)]: Task process succeded (Exit code %d)"), *GetValidatorName(), *ProcessId, ReturnCode);
	}
	else
	{
		LogFailure(FString::Printf(TEXT("[%s (%s)]: Task process failed with exit code %d and %d log errors."), *GetValidatorName(), *ProcessId, ReturnCode, ProcessesData[ProcessId].ErrorList.Num()));
	}
}

void FValidatorRunExecutable::Tick(float DeltaTime)
{		
	FValidatorBase::Tick(DeltaTime);

	if (GetIsRunning() && Processes.Num() != 0)
	{	
		bool bAnyProcessRunning = false;
		bool bAllFinished = true;
		bool bAllSucceeded = true;

		const FValidatorRunExecutableDefinition* Def = GetTypedDefinition<FValidatorRunExecutableDefinition>();

		for (const FProcessWrapper& Process : Processes)
		{
			bAnyProcessRunning |= Process.IsRunning();
			bAllFinished &= Process.bIsComplete;

			if (Process.bIsComplete)
			{
				const bool Success = Def->AllowedExitCodes.Contains(Process.ExitCode) && (Def->bOnlyLookAtExitCode || ProcessesData[Process.GetProcessName()].ErrorList.IsEmpty());
				bAllSucceeded &= Success;
			}
		}

		if (bAllFinished)
		{
			ValidationFinished(bAllSucceeded);
		}
		else
		{
			for (FProcessWrapper& Process : Processes)
			{
				if (!Process.bStarted)
				{
					if (GetTypedDefinition<FValidatorRunExecutableDefinition>()->bAllowProcessConcurrency || !bAnyProcessRunning)
					{
						const bool ProcessStarted = Process.Start();
						bAnyProcessRunning = true;
						if (ProcessStarted)
						{
							UE_LOG(LogValidators, Log, TEXT("[%s (%s)]: %s"), *GetValidatorName(), *Process.GetProcessName(), TEXT("Task process started."));
						}
						else
						{
							FString ErrorMessage = FString::Format(TEXT("Task process failed to start with Process path: '{0}' and arguments: '{1}'"),
								{
									*Process.GetExecutable(),
									*Process.GetArgs()
								});

							LogFailure(FString::Printf(TEXT("[%s (%s)]: %s"), *GetValidatorName(), *Process.GetProcessName(),  *ErrorMessage));
						}
					}
				}
			}
		}

	}
}

const TArray<FAnalyticsEventAttribute> FValidatorRunExecutable::GetTelemetryAttributes() const
{
	TArray<FAnalyticsEventAttribute> Attributes = FValidatorBase::GetTelemetryAttributes();
	AppendAnalyticsEventAttributeArray(Attributes, 
		TEXT("ProcessCount"), Processes.Num()
	);

	for (size_t i = 0; i < Processes.Num(); ++i)
	{
		const FProcessWrapper& ProcessWrapper = Processes[i];
		if (i == 0)
		{
			AppendAnalyticsEventAttributeArray(Attributes,
				TEXT("Executable"), ProcessWrapper.GetExecutable(),
				TEXT("Args"), ProcessWrapper.GetArgs(),
				TEXT("Started"), ProcessWrapper.bStarted,
				TEXT("ExeExitCode"), ProcessWrapper.ExitCode,
				TEXT("ExeRunTime"), ProcessWrapper.ExecutingTime,
				TEXT("ErrorCount"), ProcessesData[ProcessWrapper.GetProcessName()].ErrorList.Num()
			);
		}
		else
		{
			AppendAnalyticsEventAttributeArray(Attributes,
				FString::Printf(TEXT("Executable #%i"), i) , ProcessWrapper.GetExecutable(),
				FString::Printf(TEXT("Args #%i"), i), ProcessWrapper.GetArgs(),
				FString::Printf(TEXT("Started #%i"), i), ProcessWrapper.bStarted,
				FString::Printf(TEXT("ExeExitCode #%i"), i), ProcessWrapper.ExitCode,
				FString::Printf(TEXT("ExeRunTime #%i"), i), ProcessWrapper.ExecutingTime,
				FString::Printf(TEXT("ErrorCount #%i"), i), ProcessesData[ProcessWrapper.GetProcessName()].ErrorList.Num()
			);
		}

	}	
	
	return Attributes;		
}

bool FValidatorRunExecutable::DoesExecutableNeedBuilding() const
{
	const FValidatorRunExecutableDefinition* ExeDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	check(ExeDefinition != nullptr);

	bool bExeNeedsBuilding = false;

	if (IFileManager::Get().FileExists(*ExeDefinition->ExecutablePath))
	{
		FBuildVersion VersionInfo;
		if (FindBuildVersionForExecutable(ExeDefinition->ExecutablePath, VersionInfo))
		{
			if (!VersionInfo.BuildUrl.IsEmpty())
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] BuildVersion info for '%s' indicates that it is a precompiled binary"), *ValidatorName, *ExeDefinition->ExecutablePath);
				return false;
			}
			else
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] BuildVersion info for '%s' indicates that it was built locally"), *ValidatorName, *ExeDefinition->ExecutablePath);
				return true;
			}
		}
		else
		{
			UE_LOG(LogValidators, Warning, TEXT("[%s] Failed to retrieve BuildVersion info for '%s', assuming that it was locally built"), *ValidatorName, *ExeDefinition->ExecutablePath);
			return true;
		}
	}
	else
	{
		UE_LOG(LogValidators, Log, TEXT("[%s] Failed to find '%s', so it will need to be built locally"), *ValidatorName, *ExeDefinition->ExecutablePath);
		return true;
	}
}

bool FValidatorRunExecutable::FindBuildVersionForExecutable(const FString& ExecutablePath, FBuildVersion& OutBuildVersion) const
{
	const FString VersionPath = FPathViews::ChangeExtension(ExecutablePath, TEXTVIEW("version"));
	if (IFileManager::Get().FileExists(*VersionPath))
	{
		FBuildVersion BuildVersion;
		if (FBuildVersion::TryRead(VersionPath, BuildVersion))
		{
			OutBuildVersion = MoveTemp(BuildVersion);
			return true;
		}
		else
		{
			return false;
		}
	}

	const FString TargetPath = FPathViews::ChangeExtension(ExecutablePath, TEXTVIEW("target"));
	if (IFileManager::Get().FileExists(*TargetPath))
	{
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *TargetPath))
		{
			return false;
		}

		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
		TSharedPtr<FJsonObject> JsonRootObject;
		if (!FJsonSerializer::Deserialize(JsonReader, JsonRootObject))
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* JsonVersionObject;
		if (!JsonRootObject->TryGetObjectField(TEXTVIEW("version"), JsonVersionObject))
		{
			return false;
		}

		FString JsonObjectString;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonObjectString, 0);
		if (!FJsonSerializer::Serialize((*JsonVersionObject).ToSharedRef(), JsonWriter, true))
		{
			return false;
		}

		FBuildVersion BuildVersion;
		if (FBuildVersion::TryReadFromString(JsonObjectString, BuildVersion))
		{
			OutBuildVersion = MoveTemp(BuildVersion);
			return true;
		}
		else
		{
			return false;
		}

	}

	return false;
}

void FValidatorRunExecutable::PrepareExecutableOptions()
{
	const FValidatorRunExecutableDefinition* TypedDefinition = GetTypedDefinition<FValidatorRunExecutableDefinition>();
	if(TypedDefinition->ExecutableCandidates.Num() > 0)
	{
		TMap<FString, FString> Options;
		Options.Reserve(TypedDefinition->ExecutableCandidates.Num());

		FValidatorRunExecutableDefinition* ModifyableDefinition = const_cast<FValidatorRunExecutableDefinition*>(TypedDefinition);

		FString SelectedOption;
		FString* UserSelectedOption = FSubmitToolUserPrefs::Get()->ValidatorOptions.Find(OptionsProvider.GetUserPrefsKey(ExecutableOptions));
		if(UserSelectedOption != nullptr && !UserSelectedOption->StartsWith(TEXT("Auto Select"), ESearchCase::IgnoreCase))
		{
			SelectedOption = *UserSelectedOption;
		}	

		FDateTime LastWriteAccess = FDateTime::MinValue();
		FFileManagerGeneric FileManager;

		FString NewestExecutable;	

		for(TPair<FString, FString>& ExecutableCandidate : ModifyableDefinition->ExecutableCandidates)
		{
			ExecutableCandidate.Value = FConfiguration::SubstituteAndNormalizeFilename(ExecutableCandidate.Value);
			Options.Add(ExecutableCandidate.Key, ExecutableCandidate.Value);

			if(FPaths::FileExists(*ExecutableCandidate.Value))
			{
				if(TypedDefinition->bUseLatestExecutable)
				{
					FFileStatData FileModifiedDate = FileManager.GetStatData(*ExecutableCandidate.Value);
					if(FileModifiedDate.ModificationTime > LastWriteAccess)
					{
						NewestExecutable = ExecutableCandidate.Key;
						LastWriteAccess = FileModifiedDate.ModificationTime;
					}
				}
				else if(SelectedOption.IsEmpty())
				{
					SelectedOption = ExecutableCandidate.Key;
				}
			}
		}

		if(TypedDefinition->bUseLatestExecutable && TypedDefinition->ExecutableCandidates.Contains(NewestExecutable))
		{
			const FString NewestExecutableOptionKey = FString::Printf(TEXT("Auto Select (%s)"), *NewestExecutable);
			Options.Add(NewestExecutableOptionKey, TypedDefinition->ExecutableCandidates[NewestExecutable]);

			if(UserSelectedOption == nullptr || UserSelectedOption->StartsWith(TEXT("Auto Select"), ESearchCase::IgnoreCase))
			{
				SelectedOption = NewestExecutableOptionKey;
			}
		}

		OptionsProvider.InitializeValidatorOptions(ExecutableOptions, Options, SelectedOption, EValidatorOptionType::FilePath);
	}
}
