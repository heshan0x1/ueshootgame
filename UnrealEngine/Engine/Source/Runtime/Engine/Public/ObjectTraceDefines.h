// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#ifndef OBJECT_TRACE_ENABLED

#if UE_TRACE_ENABLED && !IS_PROGRAM && !UE_BUILD_SHIPPING
#define OBJECT_TRACE_ENABLED 1
#else
#define OBJECT_TRACE_ENABLED 0
#endif

#endif
