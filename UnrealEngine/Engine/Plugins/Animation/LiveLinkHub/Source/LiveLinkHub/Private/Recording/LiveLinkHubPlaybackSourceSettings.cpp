// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubPlaybackSourceSettings.h"
#include "LiveLinkHubPlaybackSourceFactory.h"
#include "LiveLinkSourceFactory.h"


ULiveLinkHubPlaybackSourceSettings::ULiveLinkHubPlaybackSourceSettings()
{
	Factory = ULiveLinkHubPlaybackSourceFactory::StaticClass();
}
