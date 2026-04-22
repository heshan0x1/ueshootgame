// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaTagSoftHandle.h"

namespace UE::AvaTransition
{

class FDefaultTags
{
	FDefaultTags();

public:
	AVALANCHETRANSITION_API static const FDefaultTags& Get();

	/** Soft handle to the default layer tag */
	FAvaTagSoftHandle DefaultLayer;
};

} // UE::AvaTransition
