// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLocalLiveLinkSubjectSettings.h"
#include "MetaHumanLocalLiveLinkSubject.h"

#include "Roles/LiveLinkBasicRole.h"
#include "InterpolationProcessor/LiveLinkBasicFrameInterpolateProcessor.h"



void UMetaHumanLocalLiveLinkSubjectSettings::Setup()
{
	Role = ULiveLinkBasicRole::StaticClass();
	InterpolationProcessor = NewObject<ULiveLinkBasicFrameInterpolationProcessor>(this);
}

void UMetaHumanLocalLiveLinkSubjectSettings::SetSubject(FMetaHumanLocalLiveLinkSubject* InSubject)
{
	Subject = InSubject;
	bIsLiveProcessing = true;
}

void UMetaHumanLocalLiveLinkSubjectSettings::ReloadSubject()
{
	if (Subject)
	{
		Subject->ReloadSubject();
	}
}

void UMetaHumanLocalLiveLinkSubjectSettings::RemoveSubject()
{
	if (Subject)
	{
		Subject->RemoveSubject();
	}
}
