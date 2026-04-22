// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistantPythonExecutor.h"

#include "Containers/UnrealString.h"
#include "Editor.h"
#include "Internationalization/Text.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"


//
// UE::AIAssistant::PythonExecutor
//


FText UE::AIAssistant::PythonExecutor::MakeTransactionTitle()
{
	// TODO Use proper LOCTEXT. But what should it be?
	return FText::FromString(FString::Printf(TEXT("AI Assistant Code Execution %s"), *FDateTime::Now().ToString()));
}


bool UE::AIAssistant::PythonExecutor::ExecutePythonScript(
	const FString& CodeString, FString* OutCodeOutputString, FText* OutTransactionTitle)
{
	if (OutCodeOutputString)
	{
		OutCodeOutputString->Empty();
	}

	if (OutTransactionTitle)
	{
		*OutTransactionTitle = FText::GetEmpty();
	}

	if (CodeString.IsEmpty())
	{
		return false;
	}
	
	
	FPythonCommandEx PythonCommand = FPythonCommandEx();
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.Command = CodeString;


	const FText TransactionTitle = MakeTransactionTitle();
	const int32 TransactionIndex = GEditor->BeginTransaction(TransactionTitle);
	const bool bWasPythonExecutionSuccessful = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommand);
	if (bWasPythonExecutionSuccessful)
	{
		GEditor->EndTransaction();
	}
	else
	{
		GEditor->CancelTransaction(TransactionIndex);
	}

	
	if (OutCodeOutputString)
	{
		if (!bWasPythonExecutionSuccessful)
		{
			// TODO Not sure why, but failing Python code, and it's errors, only appear in the log, and I don't see them in the loop below!
			*OutCodeOutputString += "Code did not execute successfully. See log for details.";
			if (PythonCommand.LogOutput.Num())
			{
				*OutCodeOutputString += "\n";
			}
		}

		const int32 Num = PythonCommand.LogOutput.Num();
		for (int32 I = 0; I < Num; I++)
		{
			const FPythonLogOutputEntry& PythonLogOutputEntry = PythonCommand.LogOutput[I];
			*OutCodeOutputString += PythonLogOutputEntry.Output;
			if (PythonLogOutputEntry.Type == EPythonLogOutputType::Error)
			{
				*OutCodeOutputString += " # ERROR";
			}
			else if (PythonLogOutputEntry.Type == EPythonLogOutputType::Warning)
			{
				*OutCodeOutputString += " # WARNING";
			}
			if (I < Num - 1)
			{
				*OutCodeOutputString += "\n";
			}
		}
	}
	
		
	if (bWasPythonExecutionSuccessful && OutTransactionTitle)
	{
		*OutTransactionTitle = TransactionTitle;
	}

	
	return bWasPythonExecutionSuccessful;
}
