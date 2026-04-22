// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

class ITargetPlatform;

// Check if unversioned property serialization is configured to be used on target platform
COREUOBJECT_API bool CanUseUnversionedPropertySerialization(const ITargetPlatform* Target);
