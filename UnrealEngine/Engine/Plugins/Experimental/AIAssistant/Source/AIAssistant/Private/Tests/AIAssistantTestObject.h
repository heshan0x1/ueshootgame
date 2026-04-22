// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "UObject/Object.h"

#include "AIAssistantTestObject.generated.h"


// Simple UObject used by tests.
UCLASS()
class UAIAssistantTestObject : public UObject
{
	GENERATED_BODY()

public:
	void SetName(const FString& InstanceName)
	{
		Name = InstanceName;
	}

	const FString& GetName() const
	{
		return Name;
	}

private:
	FString Name;
};