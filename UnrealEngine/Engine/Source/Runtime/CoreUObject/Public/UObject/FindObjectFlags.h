// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumClassFlags.h"

enum class EFindObjectFlags
{
	None,

	/** Whether to require an exact match with the passed in class */
	ExactClass
};
ENUM_CLASS_FLAGS(EFindObjectFlags)

#define UE_EXACTCLASS_BOOL_DEPRECATED(FunctionName) UE_DEPRECATED(5.7, FunctionName " with a boolean ExactClass has been deprecated - please use the EFindObjectFlags enum instead")
