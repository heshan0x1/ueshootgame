// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBackgroundHttpRequestMetricsExtended
{
	int32 TotalBytesDownloaded;
	float DownloadDuration;
	FDateTime FetchStartTimeUTC;
	FDateTime FetchEndTimeUTC;
};
