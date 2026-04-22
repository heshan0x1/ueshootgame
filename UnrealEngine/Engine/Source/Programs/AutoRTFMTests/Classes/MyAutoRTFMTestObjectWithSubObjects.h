// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MyAutoRTFMTestObjectWithSubObjects.generated.h"

UCLASS()
class UMyAutoRTFMTestObjectWithSubObjects : public UObject
{
	GENERATED_BODY()

public:
	static FName GetSubObjectName();

	UMyAutoRTFMTestObjectWithSubObjects(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UObject* SubObject;

	void DoNothing() const {}
};
