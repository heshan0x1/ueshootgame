// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

#pragma once


// TODO - Change to follow pattern of IWebJavaScriptExecutor, add tests.


//
// UE::AIAssistant::PythonExecutor
//


namespace UE::AIAssistant::PythonExecutor
{
	/**
	 * Generates a title for transactions, such as for Python code execution.
	 * @return Transaction title to use.
	 */
	FText MakeTransactionTitle();
	
	/**
	 * Executes a Python string in Unreal Editor.
	 * @param CodeString The Unreal Python code to execute.
	 * @param OutCodeOutputString Optional output string containing the code output.
	 * @param OutTransactionTitle Optional output string containing the title of the transaction.
	 * @return Whether the Unreal Python code was executed successfully.
	 */
	bool ExecutePythonScript(
		const FString& CodeString, FString* OutCodeOutputString = nullptr,
		FText* OutTransactionTitle = nullptr);
}
