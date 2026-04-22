// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreTypes.h"
#include "UObject/Interface.h"
#include "UObject/Object.h"

#include "TestInterface.generated.h"

UINTERFACE(NotBlueprintable)
class UTestInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class ITestInterface
{
	GENERATED_IINTERFACE_BODY()
public:

    UFUNCTION(BlueprintCallable, Category="Test")
    virtual FName GetNumberedName(FName BaseName, int32 InNumber) = 0;
};