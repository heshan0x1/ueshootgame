// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFaceSourceBlueprint.h"
#include "LiveLinkFaceSource.h"
#include "LiveLinkFaceSourceSettings.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"



void ULiveLinkFaceSourceBlueprint::CreateLiveLinkFaceSource(FLiveLinkSourceHandle& OutLiveLinkFaceSource, bool& bOutSucceeded)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	TSharedPtr<FLiveLinkFaceSource> LiveLinkFaceSource = MakeShared<FLiveLinkFaceSource>("");

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	const FGuid GUID = LiveLinkClient.AddSource(LiveLinkFaceSource);

	OutLiveLinkFaceSource.SetSourcePointer(LiveLinkFaceSource);

	bOutSucceeded = GUID.IsValid();
}

void ULiveLinkFaceSourceBlueprint::Connect(const FLiveLinkSourceHandle& InLiveLinkFaceSource, const FString& InSubjectName, const FString& InAddress, bool& bOutSucceeded, int32 InPort)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	if (InLiveLinkFaceSource.SourcePointer && InLiveLinkFaceSource.SourcePointer->GetSourceType().ToString() == FLiveLinkFaceSource::SourceType.ToString())
	{
		FLiveLinkFaceSource* LiveLinkFaceSource = (FLiveLinkFaceSource*) InLiveLinkFaceSource.SourcePointer.Get();

		ULiveLinkFaceSourceSettings* LiveLinkFaceSourceSettings = Cast<ULiveLinkFaceSourceSettings>(LiveLinkClient.GetSourceSettings(LiveLinkFaceSource->GetSourceGuid()));

		if (LiveLinkFaceSourceSettings)
		{
			LiveLinkFaceSourceSettings->SetAddress(InAddress);
			LiveLinkFaceSourceSettings->SetPort(InPort);
			LiveLinkFaceSourceSettings->SetSubjectName(InSubjectName);
			if (LiveLinkFaceSourceSettings->RequestConnect())
			{
				bOutSucceeded = true;
				return;
			}
		}
	}

	bOutSucceeded = false;
}
