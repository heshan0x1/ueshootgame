// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "HomeScreenCommon.generated.h"

UENUM()
enum class EMainSectionMenu : uint8
{
	None,
	Home,
	News,
	GettingStarted,
	SampleProjects
};

UENUM()
enum class EAutoLoadProject
{
	HomeScreen UMETA(DisplayName = "Home Panel"),
	LastProject UMETA(DisplayName = "Most Recent Project"),
	MAX UMETA(Hidden)
};
