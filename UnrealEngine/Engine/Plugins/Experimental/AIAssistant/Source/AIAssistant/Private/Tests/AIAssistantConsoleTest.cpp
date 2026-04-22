// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/UnrealString.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"

#include "AIAssistantConsole.h"
#include "Tests/AIAssistantTestFlags.h"
#include "Tests/AIAssistantUefnModeConsoleVar.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::AIAssistant;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConsoleTestUefnModeVariableExists,
	"AI.Assistant.Console.UefnModeVariableExists",
	AIAssistantTest::Flags);

bool FAIAssistantConsoleTestUefnModeVariableExists::RunTest(const FString& UnusedParameters)
{
	return TestTrue(
		*FString::Printf(TEXT("%s Exists"), *UefnModeConsoleVariableName),
		FindUefnModeConsoleVariable() != nullptr);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConsoleTestUefnModeSubscriptionNotifyOnConstruction,
	"AI.Assistant.Console.UefnModeSubscriptionNotifyOnConstruction",
	AIAssistantTest::Flags);

bool FAIAssistantConsoleTestUefnModeSubscriptionNotifyOnConstruction::RunTest(
	const FString& UnusedParameters)
{
	if (!FindUefnModeConsoleVariable()) return false;

	ScopedUefnModeConsoleVariableRestorer Restorer;
	bool bCalledOnConstruction = false;
	FUefnModeSubscription Subscription(
		[&bCalledOnConstruction](bool) -> void
		{
			bCalledOnConstruction = true;
		});

	return TestTrue(TEXT("NotifiedOnConstruction"), bCalledOnConstruction);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConsoleTestUefnModeSubscriptionNotifyOnChange,
	"AI.Assistant.Console.UefnModeSubscriptionNotifyOnChange",
	AIAssistantTest::Flags);

bool FAIAssistantConsoleTestUefnModeSubscriptionNotifyOnChange::RunTest(
	const FString& UnusedParameters)
{
	auto* UefnModeConsoleVariable = FindUefnModeConsoleVariable();
	if (!UefnModeConsoleVariable) return false;

	auto Restorer = MakeShared<ScopedUefnModeConsoleVariableRestorer>();
	auto NumberOfNotifications = MakeShared<int>(0);
	auto bLastValue = MakeShared<TOptional<bool>>();
	auto Subscription = MakeShared<FUefnModeSubscription>(
		[NumberOfNotifications, bLastValue](bool bValue) -> void
		{
			(*NumberOfNotifications)++;
			bLastValue->Emplace(bValue);
		});

	bool bNewValue = !IsUefnMode();
	UefnModeConsoleVariable->Set(bNewValue);

	// Console variables update notifications are batched and sent asynchronously so check for the
	// change from a latent command.
	AddCommand(
		new FDelayedFunctionLatentCommand(
			[this, NumberOfNotifications, bLastValue, UefnModeConsoleVariable, bNewValue,
			 Subscription, Restorer]
			{
				// 1 for construction + 1 for an initial change.
				int ExpectedNumberOfNotifications = 2;
				(void)TestEqual(
					TEXT("ExpectedNotifications"),
					*NumberOfNotifications, ExpectedNumberOfNotifications);

				if (bLastValue->IsSet())
				{
					(void)TestEqual(
						TEXT("NotifiedWithValue"), bNewValue, bLastValue->GetValue());
				}
			},
			ConsoleVariableUpdateDelayInSeconds));
	
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConsoleTestUefnModeSubscriptionNotNotifiedWithNoChange,
	"AI.Assistant.Console.UefnModeSubscriptionNotNotifiedWithNoChange",
	AIAssistantTest::Flags);

bool FAIAssistantConsoleTestUefnModeSubscriptionNotNotifiedWithNoChange::RunTest(
	const FString& UnusedParameters)
{
	auto* UefnModeConsoleVariable = FindUefnModeConsoleVariable();
	if (!UefnModeConsoleVariable) return false;

	auto Restorer = MakeShared<ScopedUefnModeConsoleVariableRestorer>();
	auto NumberOfNotifications = MakeShared<int>(0);
	auto Subscription = MakeShared<FUefnModeSubscription>(
		[NumberOfNotifications](bool) -> void
		{
			(*NumberOfNotifications)++;
		});

	bool bNewValue = IsUefnMode();
	UefnModeConsoleVariable->Set(bNewValue);

	// Console variables update notifications are batched and sent asynchronously so check for the
	// change from a latent command.
	AddCommand(
		new FDelayedFunctionLatentCommand(
			[this, NumberOfNotifications, UefnModeConsoleVariable, Subscription, Restorer]
			{
				int ExpectedNumberOfNotifications = 1;  // 1 for construction
				(void)TestEqual(
					TEXT("ExpectedNotifications"),
					*NumberOfNotifications, ExpectedNumberOfNotifications);
			},
			ConsoleVariableUpdateDelayInSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConsoleTestUefnModeSubscriptionNotNotifiedWhenUnsubscribed,
	"AI.Assistant.Console.UefnModeSubscriptionNotNotifiedWhenUnsubscribed",
	AIAssistantTest::Flags);

bool FAIAssistantConsoleTestUefnModeSubscriptionNotNotifiedWhenUnsubscribed::RunTest(
	const FString& UnusedParameters)
{
	auto* UefnModeConsoleVariable = FindUefnModeConsoleVariable();
	if (!UefnModeConsoleVariable) return false;

	auto Restorer = MakeShared<ScopedUefnModeConsoleVariableRestorer>();
	auto NumberOfNotifications = MakeShared<int>(0);
	{
		FUefnModeSubscription Subscription(
			[NumberOfNotifications](bool) -> void
			{
				(*NumberOfNotifications)++;
			});
	}

	bool bNewValue = !IsUefnMode();
	UefnModeConsoleVariable->Set(bNewValue);

	// Console variables update notifications are batched and sent asynchronously so check for the
	// change from a latent command.
	AddCommand(
		new FDelayedFunctionLatentCommand(
			[this, NumberOfNotifications, UefnModeConsoleVariable, Restorer]
			{
				int ExpectedNumberOfNotifications = 1;  // 1 for construction
				(void)TestEqual(
					TEXT("ExpectedNotifications"),
					*NumberOfNotifications, ExpectedNumberOfNotifications);
			},
			ConsoleVariableUpdateDelayInSeconds));
	return true;
}

#endif