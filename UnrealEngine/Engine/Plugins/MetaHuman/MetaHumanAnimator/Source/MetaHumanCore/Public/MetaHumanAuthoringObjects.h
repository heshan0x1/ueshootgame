// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

#define UE_API METAHUMANCORE_API



class FMetaHumanAuthoringObjects
{
public:

	static UE_API bool ArePresent();

	static UE_API bool FindObject(FString& InOutObjectPath);
	static UE_API bool FindObject(FString& InOutObjectPath, bool& bOutWasFound, bool& bOutHasMoved);

	template<class T>
	static bool FindObject(TSoftObjectPtr<T>& InSoftObjectPtr)
	{
		bool bWasFound = false;
		bool bHasMoved = false;
		return FindObject(InSoftObjectPtr, bWasFound, bHasMoved);
	}

	template<class T>
	static bool FindObject(TSoftObjectPtr<T>& InSoftObjectPtr, bool& bOutWasFound, bool& bOutHasMoved)
	{
		FSoftObjectPath SoftObjectPath = InSoftObjectPtr.ToSoftObjectPath();
		FString Path = SoftObjectPath.GetAssetPathString();

		bool bFindObjectFailed = FindObject(Path, bOutWasFound, bOutHasMoved);

		SoftObjectPath.SetPath(Path);
		InSoftObjectPtr = SoftObjectPath;

		return bFindObjectFailed;
	}
};

#undef UE_API
