// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "HomeScreenMenuContext.generated.h"

class SHomeScreen;

UCLASS(MinimalAPI)
class UHomeScreenContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SHomeScreen> HomeScreen;
};
