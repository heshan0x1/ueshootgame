// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

namespace UE::IREEUtils
{

IREEUTILS_API bool ResolveEnvironmentVariables(FString& String);

IREEUTILS_API void RunCommand(const FString& Command, const FString& Arguments, const FString& WorkingDir, const FString& LogFilePath = FString());

IREEUTILS_API bool ImportOnnx(const FString& ImporterCommand, const FString& ImporterArguments, TConstArrayView<uint8> InFileData, const FString& InModelName, const FString& InOutputDir, TArray64<uint8>& OutMlirData);

}