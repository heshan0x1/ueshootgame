// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingAssetBase.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothingAssetBase)

DEFINE_LOG_CATEGORY(LogClothingAsset)

#if WITH_EDITOR
void UClothingAssetBase::WarningNotification(const FText& Text)
{
	FNotificationInfo* const NotificationInfo = new FNotificationInfo(Text);
	NotificationInfo->ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().QueueNotification(NotificationInfo);

	UE_LOG(LogClothingAsset, Warning, TEXT("%s"), *Text.ToString());
}
#endif
