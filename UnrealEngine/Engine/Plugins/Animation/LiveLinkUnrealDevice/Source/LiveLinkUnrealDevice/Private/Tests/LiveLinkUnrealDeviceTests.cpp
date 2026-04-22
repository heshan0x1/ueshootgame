// Copyright Epic Games, Inc. All Rights Reserved.

#include "Devices/LiveLinkUnrealDevice.h"
#include "Misc/AutomationTest.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealDeviceTest, "LiveLinkHub.UnrealDevice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


bool FUnrealDeviceTest::RunTest(const FString& Parameters)
{
	ULiveLinkUnrealDevice* Device = NewObject<ULiveLinkUnrealDevice>();

	return true;
}
