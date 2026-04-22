// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

namespace UE::CaptureManager
{

bool ExtractInfoFromFileName(const FString& InFileName, FString& OutPrefix, FString& OutDigits, FString& OutExtension);

FString GetFileFormat(const FString& InFileName);
FString GetFileNameFormat(const FString& InDirectory);

}