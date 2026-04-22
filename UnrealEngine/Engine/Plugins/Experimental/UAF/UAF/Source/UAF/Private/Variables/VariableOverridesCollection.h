// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VariableOverrides.h"

namespace UE::UAF
{

// A collection of overrides for asset/struct variables
struct FVariableOverridesCollection
{
	TArray<FVariableOverrides> Collection;
};

}