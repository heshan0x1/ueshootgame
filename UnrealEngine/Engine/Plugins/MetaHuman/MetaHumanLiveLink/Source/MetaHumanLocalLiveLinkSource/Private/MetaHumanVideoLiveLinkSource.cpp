// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanVideoLiveLinkSource.h"
#include "MetaHumanVideoLiveLinkSourceSettings.h"
#include "MetaHumanVideoLiveLinkSubject.h"
#include "MetaHumanVideoLiveLinkSubjectSettings.h"

#define LOCTEXT_NAMESPACE "MetaHumanVideoLiveLinkSource"



FText FMetaHumanVideoLiveLinkSource::SourceType = LOCTEXT("MetaHumanVideo", "MetaHuman (Video)");

FText FMetaHumanVideoLiveLinkSource::GetSourceType() const
{
	return FMetaHumanVideoLiveLinkSource::SourceType;
}

TSubclassOf<ULiveLinkSourceSettings> FMetaHumanVideoLiveLinkSource::GetSettingsClass() const
{
	return UMetaHumanVideoLiveLinkSourceSettings::StaticClass();
}

TSharedPtr<FMetaHumanLocalLiveLinkSubject> FMetaHumanVideoLiveLinkSource::CreateSubject(const FName& InSubjectName, UMetaHumanLocalLiveLinkSubjectSettings* InSettings)
{
	return MakeShared<FMetaHumanVideoLiveLinkSubject>(LiveLinkClient, SourceGuid, InSubjectName, Cast<UMetaHumanVideoLiveLinkSubjectSettings>(InSettings));
}

#undef LOCTEXT_NAMESPACE
