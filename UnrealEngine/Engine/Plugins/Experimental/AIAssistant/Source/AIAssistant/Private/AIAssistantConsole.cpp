// Copyright Epic Games, Inc. All Rights Reserved.
#include "AIAssistantConsole.h"

#include "Async/UniqueLock.h"
#include "Containers/UnrealString.h"
#include "HAL/IConsoleManager.h"
#include "Templates/Function.h"
#include "Templates/UnrealTemplate.h"

namespace UE::AIAssistant
{
	constexpr bool bDefaultUefnMode = false;

	// IMPORTANT: Do not rename this without ensuring all references in Fortnite are updated.
	const FString UefnModeConsoleVariableName(TEXT("ai.assistant.uefn"));

	bool bIsUefnModeConsoleVariableValue = bDefaultUefnMode;

	// Currently selected AI assistant profile.
	FAutoConsoleVariableRef ProfileConsoleVariableRef(
		*UefnModeConsoleVariableName, bIsUefnModeConsoleVariableValue,
		TEXT("Controls the currently selected AI assistant profile UE vs. UEFN."));

	bool FUefnModeSubscription::bPreviousIsUefnModeConsoleVariableValue = bDefaultUefnMode;
	UE::FMutex FUefnModeSubscription::SubscriptionsLock;
	TDoubleLinkedList<FUefnModeSubscription*> FUefnModeSubscription::Subscriptions;

	FUefnModeSubscription::FUefnModeSubscription(TFunction<void(bool)>&& OnUefnModeFunction) :
		OnUefnMode(MoveTemp(OnUefnModeFunction))
	{
		{
			UE::TUniqueLock Lock(SubscriptionsLock);
			Subscriptions.AddHead(this);
		}
		InitializeUefnModeConsoleVariableSink();
		OnUefnMode(bIsUefnModeConsoleVariableValue);
	}

	FUefnModeSubscription::~FUefnModeSubscription()
	{
		UE::TUniqueLock Lock(SubscriptionsLock);
		Subscriptions.RemoveNode(this);
	}

	void FUefnModeSubscription::InitializeUefnModeConsoleVariableSink()
	{
		static FAutoConsoleVariableSink SubscriptionSink(
			FConsoleCommandDelegate::CreateStatic(NotifyUefnModeConsoleVariableUpdated));
		(void)SubscriptionSink;
		bPreviousIsUefnModeConsoleVariableValue = bIsUefnModeConsoleVariableValue;
	}

	void FUefnModeSubscription::NotifyUefnModeConsoleVariableUpdated()
	{
		UE::TUniqueLock Lock(SubscriptionsLock);
		bool bIsUefnMode = bIsUefnModeConsoleVariableValue;
		if (bPreviousIsUefnModeConsoleVariableValue != bIsUefnMode)
		{
			for (auto& Subscription : Subscriptions)
			{
				Subscription->OnUefnMode(bIsUefnModeConsoleVariableValue);
			}
			bPreviousIsUefnModeConsoleVariableValue = bIsUefnMode;
		}
	}
}