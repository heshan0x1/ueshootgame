// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::UAF
{

enum class ETaskRunLocation : int32
{
	// Run the task before the specified task
	Before,

	// Run the task after the specified task
	After,
};

}