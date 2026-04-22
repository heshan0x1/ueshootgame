// Copyright Epic Games, Inc. All Rights Reserved.

#include "IREEUtils.h"

#include "HAL/PlatformFileManager.h"
#include "IREEUtilsLog.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"

namespace UE::IREEUtils
{

bool ResolveEnvironmentVariables(FString& String)
{
	const FString StartString = "$ENV{";
	const FString EndString = "}";
	FString ResultString = String;
	int32 StartIndex = ResultString.Find(StartString, ESearchCase::CaseSensitive);
	while (StartIndex != INDEX_NONE)
	{
		StartIndex += StartString.Len();
		int32 EndIndex = ResultString.Find(EndString, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
		if (EndIndex > StartIndex)
		{
			FString EnvironmentVariableName = ResultString.Mid(StartIndex, EndIndex - StartIndex);
			FString EnvironmentVariableValue = FPlatformMisc::GetEnvironmentVariable(*EnvironmentVariableName);
			if (EnvironmentVariableValue.IsEmpty())
			{
				return false;
			}
			else
			{
				ResultString.ReplaceInline(*(StartString + EnvironmentVariableName + EndString), *EnvironmentVariableValue, ESearchCase::CaseSensitive);
			}
		}
		else
		{
			return false;
		}
		StartIndex = ResultString.Find(StartString, ESearchCase::CaseSensitive);
	}
	String = ResultString;
	return true;
}

void RunCommand(const FString& Command, const FString& Arguments, const FString& WorkingDir, const FString& LogFilePath)
{
	int32 ReturnCode = 0;
	bool IsCanceled = false;

	FMonitoredProcess Process(Command, Arguments, WorkingDir, true);
	Process.OnCompleted().BindLambda([&ReturnCode] (int32 _ReturnCode) { ReturnCode = _ReturnCode; });
	Process.OnCanceled().BindLambda([&IsCanceled] (){ IsCanceled = true; });

	if (!Process.Launch())
	{
		UE_LOG(LogIREEUtils, Warning, TEXT("Failed to launch subprocess!"));
		return;
	}

	while (Process.Update())
	{
		// Poll until process has finished
	}

	if (IsCanceled)
	{
		UE_LOG(LogIREEUtils, Warning, TEXT("Execution of subprocess was canceled!"));
	}
	else if (ReturnCode)
	{
		UE_LOG(LogIREEUtils, Warning, TEXT("Subprocess exited with non-zero code %d"), ReturnCode);
	}

	if (!LogFilePath.IsEmpty())
	{
		const FString Output = Process.GetFullOutputWithoutDelegate();

		FStringBuilderBase Builder;
		Builder.Append(Command)
			.Append(TEXT(" "))
			.Append(Arguments)
			.Append(LINE_TERMINATOR LINE_TERMINATOR)
			.Append(Output);

		FFileHelper::SaveStringToFile(Builder.ToString(), *LogFilePath);

		UE_LOG(LogIREEUtils, Log, TEXT("Saved subprocess output to: %s"), *LogFilePath);
	}
}

bool ImportOnnx(const FString& ImporterCommand, const FString& ImporterArguments, TConstArrayView<uint8> InFileData, const FString& InModelName, const FString& InOutputDir, TArray64<uint8>& OutMlirData)
{
	SCOPED_NAMED_EVENT_TEXT("IREEUtils::ImportOnnx", FColor::Magenta);

	using namespace Private;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString InputFilePath = FPaths::Combine(InOutputDir, InModelName) + ".onnx";
	if (!PlatformFile.FileExists(*InputFilePath))
	{
		SCOPED_NAMED_EVENT_TEXT("InputFile", FColor::Magenta);

		if(!FFileHelper::SaveArrayToFile(InFileData, *InputFilePath))
		{
			UE_LOG(LogIREEUtils, Warning, TEXT("IREECompilerRDG failed to save ONNX model \"%s\""), *InputFilePath);
			return false;
		}
	}

	FString OutputFilePath = FPaths::Combine(InOutputDir, InModelName) + ".mlir";
	FString IntermediateFilePathNoExt = FPaths::Combine(InOutputDir, InModelName);

	FString ImporterArgumentsCopy = ImporterArguments;
	if (!IREEUtils::ResolveEnvironmentVariables(ImporterArgumentsCopy))
	{
		UE_LOG(LogIREEUtils, Warning, TEXT("IREECompilerRDG could not replace environment variables in %s"), *ImporterArgumentsCopy);
		return false;
	}
	ImporterArgumentsCopy.ReplaceInline(*FString("${INPUT_PATH}"), *(FString("\"") + InputFilePath + "\""));
	ImporterArgumentsCopy.ReplaceInline(*FString("${OUTPUT_PATH}"), *(FString("\"") + OutputFilePath + "\""));

	{
		SCOPED_NAMED_EVENT_TEXT("Import", FColor::Magenta);

		IREEUtils::RunCommand(ImporterCommand, ImporterArgumentsCopy, FPaths::RootDir(), IntermediateFilePathNoExt + "_import-log.txt");
	}

	if (!PlatformFile.FileExists(*OutputFilePath))
	{
		UE_LOG(LogIREEUtils, Warning, TEXT("IREECompilerRDG failed to import the model \"%s\" using the command:"), *InputFilePath);
		UE_LOG(LogIREEUtils, Warning, TEXT("\"%s\" %s"), *ImporterCommand, *ImporterArgumentsCopy);
		return false;
	}

	{
		SCOPED_NAMED_EVENT_TEXT("Load", FColor::Magenta);
		if(!FFileHelper::LoadFileToArray(OutMlirData, *OutputFilePath))
		{
			UE_LOG(LogIREEUtils, Warning, TEXT("IREECompilerRDG failed to load imported model \"%s\""), *OutputFilePath);
			return false;
		}
	}

	return true;
}

} // namespace UE::IREEUtils