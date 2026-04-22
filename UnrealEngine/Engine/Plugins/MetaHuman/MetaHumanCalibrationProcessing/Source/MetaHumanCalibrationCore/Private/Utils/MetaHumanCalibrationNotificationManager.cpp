// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanCalibrationNotificationManager.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "OutputLogModule.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationNotificationManager"

void FMetaHumanCalibrationNotificationManager::NotificationOnBegin(const FText& InInfoText)
{
	if (IsInGameThread())
	{
		NotificationOnBegin_GameThread(InInfoText);
	}
	else
	{
		ExecuteOnGameThread(TEXT("CalibrationNotificationOnBegin"), [This = AsShared(), InInfoText]()
							{
								This->NotificationOnBegin_GameThread(InInfoText);
							});
	}
}

void FMetaHumanCalibrationNotificationManager::NotificationOnEnd(bool bIsSuccess, TOptional<FText> InSubInfoText)
{
	if (IsInGameThread())
	{
		NotificationOnEnd_GameThread(bIsSuccess, MoveTemp(InSubInfoText));
	}
	else
	{
		ExecuteOnGameThread(TEXT("CalibrationNotificationOnEnd"), [This = AsShared(), bIsSuccess, SubText = MoveTemp(InSubInfoText)]() mutable
							{
								This->NotificationOnEnd_GameThread(bIsSuccess, MoveTemp(SubText));
							});
	}
}

void FMetaHumanCalibrationNotificationManager::NotificationOnBegin_GameThread(const FText& InInfoText)
{
	FNotificationInfo Info(InInfoText);
	Info.bFireAndForget = false;
	Info.ExpireDuration = 1.0f;

	FScopeLock Lock(&Mutex);
	checkf(!CurrentNotification.IsValid(), TEXT("Missing NotificationOnEnd call"));

	CurrentNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (CurrentNotification)
	{
		CurrentNotification->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FMetaHumanCalibrationNotificationManager::NotificationOnEnd_GameThread(bool bIsSuccess, TOptional<FText> InSubInfoText)
{
	FScopeLock Lock(&Mutex);
	checkf(CurrentNotification.IsValid(), TEXT("Missing NotificationOnBegin call"));

	if (!bIsSuccess)
	{
		CurrentNotification->SetHyperlink(FSimpleDelegate::CreateLambda([]()
											{
												FOutputLogModule& OutputLogModule = FModuleManager::Get().LoadModuleChecked<FOutputLogModule>("OutputLog");
												OutputLogModule.FocusOutputLog();
											}), LOCTEXT("CalibrationOpenLog", "Open Output Log"));

		CurrentNotification->SetExpireDuration(5.0f);
		CurrentNotification->SetCompletionState(SNotificationItem::CS_Fail);
	}
	else
	{
		if (InSubInfoText.IsSet())
		{
			CurrentNotification->SetSubText(InSubInfoText.GetValue());
		}

		CurrentNotification->SetCompletionState(SNotificationItem::CS_Success);
	}

	CurrentNotification->ExpireAndFadeout();
	CurrentNotification = nullptr;
}

#undef LOCTEXT_NAMESPACE