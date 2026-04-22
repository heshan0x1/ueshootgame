// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundCueDetails.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "SoundBaseDetailsUtils.h"

#define LOCTEXT_NAMESPACE "FSoundCueDetails"

namespace SoundCueDetailsUtils
{
	static const TArray<FName> CategoryOrder =
	{
		"Sound",
		"Attenuation",
		"Effects",
		"Voice Management",
		"AudioProperties",
		"Memory",
		"Advanced",
		"Developer",
	};

	void SortCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap)
	{
		FSoundBaseDetailsUtils::SortSoundCategories(AllCategoryMap, SoundCueDetailsUtils::CategoryOrder);
	}
} // namespace SoundCueDetailsUtils

TSharedRef<IDetailCustomization> FSoundCueDetails::MakeInstance()
{
	return MakeShared<FSoundCueDetails>();
}

void FSoundCueDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.SortCategories(SoundCueDetailsUtils::SortCategories);
}

#undef LOCTEXT_NAMESPACE
