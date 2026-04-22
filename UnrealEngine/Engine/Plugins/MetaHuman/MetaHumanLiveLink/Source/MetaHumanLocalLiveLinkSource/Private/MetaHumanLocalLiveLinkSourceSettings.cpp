// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLocalLiveLinkSourceSettings.h"
#include "MetaHumanLocalLiveLinkSource.h"



void UMetaHumanLocalLiveLinkSourceSettings::SetSource(FMetaHumanLocalLiveLinkSource* InSource)
{
	Source = InSource;
}

FLiveLinkSubjectKey UMetaHumanLocalLiveLinkSourceSettings::RequestSubjectCreation(const FString& InSubjectName, UMetaHumanLocalLiveLinkSubjectSettings* InMetaHumanLocalLiveLinkSubjectSettings)
{
	return Source->RequestSubjectCreation(InSubjectName, InMetaHumanLocalLiveLinkSubjectSettings);
}
