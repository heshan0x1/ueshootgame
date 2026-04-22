// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Color.h"

#define UE_API UAFANIMGRAPHUNCOOKEDONLY_API

namespace UE::UAF::UncookedOnly
{

struct FGraphNodeColors
{
	static UE_API FLinearColor Blends;
	static UE_API FLinearColor Controls;
	static UE_API FLinearColor SubGraphs;
	static UE_API FLinearColor Generators;
};

}

#undef UE_API