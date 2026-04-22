// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanVideoBaseLiveLinkSubjectSettings.h"

#include "MetaHumanVideoBaseLiveLinkSubject.h"
#include "MetaHumanVideoLiveLinkSettings.h"



UMetaHumanVideoBaseLiveLinkSubjectSettings::UMetaHumanVideoBaseLiveLinkSubjectSettings()
{
	const UMetaHumanVideoLiveLinkSettings* DefaultSettings = GetDefault<UMetaHumanVideoLiveLinkSettings>();

	bHeadOrientation = DefaultSettings->bHeadOrientation;
	bHeadTranslation = DefaultSettings->bHeadTranslation;
	MonitorImage = DefaultSettings->MonitorImage;

	// Width of the image monitor widget is somewhat arbitrary since it is always
	// placed in a layout which fills the horizontal space available to it.
	// That "fill to width" layout takes precedence over the desired width of the
	// widget we are setting here. If the image monitor widget were to be placed 
	// in a horizontally scrolling layout this would no longer work.
	MonitorImageSize = FVector2D(1, DefaultSettings->MonitorImageHeight); 
}

#if WITH_EDITOR
void UMetaHumanVideoBaseLiveLinkSubjectSettings::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		const FName PropertyName = *Property->GetName();

		const bool bHeadOrientationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadOrientation);
		const bool bHeadTranslationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadTranslation);
		const bool bHeadStabilizationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadStabilization);
		const bool bMonitorImageChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MonitorImage);
		const bool bRotationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Rotation);

		FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;

		if (bHeadOrientationChanged)
		{
			VideoSubject->SetHeadOrientation(bHeadOrientation);
		}
		else if (bHeadTranslationChanged)
		{
			VideoSubject->SetHeadTranslation(bHeadTranslation);
		}
		else if (bHeadStabilizationChanged)
		{
			VideoSubject->SetHeadStabilization(bHeadStabilization);
		}
		else if (bMonitorImageChanged)
		{
			VideoSubject->SetMonitorImage(MonitorImage);
		}
		else if (bRotationChanged)
		{
			VideoSubject->SetRotation(Rotation);
		}
	}
}
#endif

void UMetaHumanVideoBaseLiveLinkSubjectSettings::CaptureNeutralHeadPose()
{
	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->MarkNeutralFrame();

	Super::CaptureNeutralHeadPose();
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadOrientation(bool bInHeadOrientation)
{
	bHeadOrientation = bInHeadOrientation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadOrientation(bHeadOrientation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadOrientation(bool& bOutHeadOrientation) const
{
	bOutHeadOrientation = bHeadOrientation;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadTranslation(bool bInHeadTranslation)
{
	bHeadTranslation = bInHeadTranslation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadTranslation(bHeadTranslation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadTranslation(bool& bOutHeadTranslation) const
{
	bOutHeadTranslation = bHeadTranslation;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadStabilization(bool bInHeadStabilization)
{
	bHeadStabilization = bInHeadStabilization;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadStabilization(bHeadStabilization);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadStabilization(bool& bOutHeadStabilization) const
{
	bOutHeadStabilization = bHeadStabilization;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetMonitorImage(EHyprsenseRealtimeNodeDebugImage InMonitorImage)
{
	MonitorImage = InMonitorImage;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetMonitorImage(MonitorImage);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetMonitorImage(EHyprsenseRealtimeNodeDebugImage& OutMonitorImage) const
{
	OutMonitorImage = MonitorImage;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetRotation(EMetaHumanVideoRotation InRotation)
{
	Rotation = InRotation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetRotation(Rotation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetRotation(EMetaHumanVideoRotation& OutRotation) const
{
	OutRotation = Rotation;
}
