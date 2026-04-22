// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"

#define VERSE_UNREACHABLE()                                                           \
	do                                                                                \
	{                                                                                 \
		FDebug::DumpStackTraceToLog(TEXT("VERSE_UNREACHABLE"), ELogVerbosity::Error); \
		while (true)                                                                  \
		{                                                                             \
			PLATFORM_BREAK();                                                         \
		}                                                                             \
	}                                                                                 \
	while (false)
