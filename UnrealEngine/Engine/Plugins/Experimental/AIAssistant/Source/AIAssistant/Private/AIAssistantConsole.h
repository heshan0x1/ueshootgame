// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Async/Mutex.h"
#include "Containers/List.h"
#include "Containers/UnrealString.h"
#include "Templates/Function.h"

namespace UE::AIAssistant
{
	// UE vs. UEFN assistant mode.
	extern const FString UefnModeConsoleVariableName;

	// Receives notifications if the console variable that controls the assistant profile changes.
	class FUefnModeSubscription
	{
	public:
		// The supplied function is called once on construction with the current mode and again if
		// the mode changes.
		FUefnModeSubscription(TFunction<void(bool)>&& OnUefnModeFunction);

		// Prevent copy.
		FUefnModeSubscription(const FUefnModeSubscription&) = delete;
		FUefnModeSubscription& operator=(const FUefnModeSubscription&) = delete;

		~FUefnModeSubscription();

	private:
		// Initializes subscription to console variable changes.
		static void InitializeUefnModeConsoleVariableSink();

		// Called when the mode console variable is updated.
		static void NotifyUefnModeConsoleVariableUpdated();

	private:
		TFunction<void(bool)> OnUefnMode;
		

	private:
		static UE::FMutex SubscriptionsLock;
		static TDoubleLinkedList<FUefnModeSubscription*> Subscriptions;
		static bool bPreviousIsUefnModeConsoleVariableValue;
	};
}