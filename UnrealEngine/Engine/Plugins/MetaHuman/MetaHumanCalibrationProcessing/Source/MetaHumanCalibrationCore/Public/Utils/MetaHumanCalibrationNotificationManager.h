// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Framework/Notifications/NotificationManager.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class FMetaHumanCalibrationNotificationManager : 
	public TSharedFromThis<FMetaHumanCalibrationNotificationManager>
{
public:

	UE_API void NotificationOnBegin(const FText& InInfoText);
	UE_API void NotificationOnEnd(bool bIsSuccess, TOptional<FText> InSubInfoText = TOptional<FText>());

private:

	void NotificationOnBegin_GameThread(const FText& InInfoText);
	void NotificationOnEnd_GameThread(bool bIsSuccess, TOptional<FText> InSubInfoText);

	FCriticalSection Mutex;
	TSharedPtr<SNotificationItem> CurrentNotification;
};

#undef UE_API