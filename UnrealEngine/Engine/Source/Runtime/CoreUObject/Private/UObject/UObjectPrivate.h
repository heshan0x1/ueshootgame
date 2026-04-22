// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUObjectBootstrap, Display, Display);

// Processes the first stage of compiled in UObjects i.e. creating them and adding them to the global array and hash
// Does not do further initialization i.e. linking classes/structs/properties
void UObjectProcessRegistrants();
