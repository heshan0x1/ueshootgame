// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectUserAssetTagSettings.h"

TSet<FName> UProjectUserAssetTagSettings::GetProjectUserAssetTagsForClass(const UClass* Class) const
{
	FSoftClassPath ClassPath(Class);
	if(UserAssetTagsPerType.Contains(ClassPath))
	{
		return UserAssetTagsPerType[ClassPath].FavoriteUserAssetTags;
	}

	return {};
}

FName UProjectUserAssetTagSettings::GetCategoryName() const
{
	return NAME_Editor;
}
