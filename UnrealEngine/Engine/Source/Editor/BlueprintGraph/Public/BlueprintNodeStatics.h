// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompilerMisc.h"

#define UE_API BLUEPRINTGRAPH_API

class UEdGraphPin;
class UFunction;
class UK2Node;
template <typename FuncType> class TFunctionRef;

class FBlueprintNodeStatics
{
public:

	static UE_API UEdGraphPin* CreateSelfPin(UK2Node* Node, const UFunction* Function);

	static UE_API bool CreateParameterPinsForFunction(UK2Node* Node, const UFunction* Function, TFunctionRef<void(UEdGraphPin* /*Pin*/)> PostParameterPinCreatedCallback);
};

#undef UE_API
