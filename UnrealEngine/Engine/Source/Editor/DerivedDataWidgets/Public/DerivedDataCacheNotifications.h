// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCacheInterface.h"
#include "IDerivedDataCacheNotifications.h"
#include "Templates/SharedPointer.h"

#define UE_API DERIVEDDATAWIDGETS_API

class SNotificationItem;

class FDerivedDataCacheNotifications : public IDerivedDataCacheNotifications
{
public:
	UE_API FDerivedDataCacheNotifications();
	UE_API virtual ~FDerivedDataCacheNotifications();

private: 

	/** DDC data put notification handler */
	UE_API void OnDDCNotificationEvent(FDerivedDataCacheInterface::EDDCNotification DDCNotification);
	
	/** Subscribe to the notifications */
	UE_API void Subscribe(bool bSubscribe);

	/** Whether we are subscribed or not **/
	bool bSubscribed;

	/** Valid when a DDC notification item is being presented */
	TSharedPtr<SNotificationItem> SharedDDCNotification;
};

#undef UE_API
