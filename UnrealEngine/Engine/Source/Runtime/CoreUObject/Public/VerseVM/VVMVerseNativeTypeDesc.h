// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "VerseScope.h"

/** Fully qualified name of a VNI package. */
struct FVniPackageName
{
	const TCHAR* const MountPointName;
	const TCHAR* const CppModuleName;
};

/** Describes a Verse package associated with a C++ module. */
struct FVniPackageDesc
{
	const FVniPackageName Name;
	const TCHAR* VersePath;             // Verse path of the root module of this package
	const EVerseScope::Type VerseScope; // Origin/visibility of Verse code in this package
	const TCHAR* const VerseDirectoryPath;
	// All dependencies needed to _incrementally_ compile this package
	// I.e. private UBT module dependencies plus transitive closure of public UBT module dependencies
	const FVniPackageName* const Dependencies;
	const int32 NumDependencies;
};

/** Describes the definition of a type */
struct FVniTypeDesc
{
	const TCHAR* const UEPackageName;
	const TCHAR* const UEName;

	const FVniPackageDesc* VersePackageDesc{nullptr};
	const TCHAR* VersePackageName{nullptr};
	const TCHAR* VerseModulePath{nullptr};
	const TCHAR* VerseScopeName{nullptr};
};
