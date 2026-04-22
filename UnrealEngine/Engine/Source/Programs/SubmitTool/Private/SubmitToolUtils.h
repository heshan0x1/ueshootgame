// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Widgets/SWindow.h"

class FString;

class FSubmitToolUtils
{
public:
	static FString GetLocalAppDataPath();

	static void CopyDiagnosticFilesToClipboard(TConstArrayView<FString> Files);

	static void EnsureWindowIsInView(TSharedRef<SWindow> InWindow, bool bSingleWindow);

	static bool IsFileInHierarchy(const FString& InWildcard, const FString& InPath);

	static void CacheWildcardToHierarchy(const FString& InWildcard, const FString& InPath);

private:
	static TMap<FString, TMap<bool, TSet<FString>>> HierarchyWildcardsCache;
};
