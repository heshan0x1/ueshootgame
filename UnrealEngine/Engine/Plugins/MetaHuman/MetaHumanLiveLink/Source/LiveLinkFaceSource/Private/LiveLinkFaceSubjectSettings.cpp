// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFaceSubjectSettings.h"



// Could use BlueprintReadWrite on the UPROPERTY, but get/set functions give you better control
// over what Category is used

void ULiveLinkFaceSubjectSettings::SetHeadOrientation(bool bInHeadOrientation)
{
	bHeadOrientation = bInHeadOrientation;
}

void ULiveLinkFaceSubjectSettings::GetHeadOrientation(bool& bOutHeadOrientation) const
{
	bOutHeadOrientation = bHeadOrientation;
}

void ULiveLinkFaceSubjectSettings::SetHeadTranslation(bool bInHeadTranslation)
{
	bHeadTranslation = bInHeadTranslation;
}

void ULiveLinkFaceSubjectSettings::GetHeadTranslation(bool& bOutHeadTranslation) const
{
	bOutHeadTranslation = bHeadTranslation;
}
