// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DataStorage/CommonTypes.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"

#include "TedsTypeInfoColumns.generated.h"

USTRUCT(meta = (DisplayName = "Row is a type"))
struct FDataStorageTypeInfoTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Row type is a class"))
struct FDataStorageClassTypeInfoTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Row type is a struct"))
struct FDataStorageStructTypeInfoTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Row type is an Interface"))
struct FDataStorageTypeInfoInterfaceTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Row type is a Verse type"))
struct FDataStorageVerseTypeInfoTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Verse type access level", EditorDataStorage_DynamicColumnTemplate))
struct FDataStorageVerseTypeInfoAccessLevel : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

namespace UE::Editor::DataStorage
{
	using FTypeInfoTag = FDataStorageTypeInfoTag;
	using FClassTypeInfoTag = FDataStorageClassTypeInfoTag;
	using FStructTypeInfoTag = FDataStorageStructTypeInfoTag;
	using FTypeInfoInterfaceTag = FDataStorageTypeInfoInterfaceTag;
	using FVerseTypeInfoTag = FDataStorageVerseTypeInfoTag;
	using FVerseTypeInfoAccessLevel = FDataStorageVerseTypeInfoAccessLevel;
}

namespace UE::Editor::DataStorage::TypeInfo
{
	static const FName TypeMappingDomain = "TypeMappingDomain";
}