// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_SUPERLUMINAL

#define PERFORMANCEAPI_ENABLED 1
#include "Superluminal/PerformanceAPI_capi.h"


void SuperLuminalStartScopedEventWide(const wchar_t* inID, const wchar_t* inData, uint32_t inColor)
{
	PerformanceAPI_BeginEvent_Wide(inID, inData, inColor);
}

void SuperLuminalStartScopedEvent(const char* inID, const char* inData, uint32_t inColor)
{
	PerformanceAPI_BeginEvent(inID, inData, inColor);
}

void SuperLuminalEndScopedEvent()
{
	PerformanceAPI_EndEvent();
}

#else

#include <stdint.h>

void SuperLuminalStartScopedEventWide(const wchar_t* inID, const wchar_t* inData, uint32_t inColor)
{
}

void SuperLuminalStartScopedEvent(const char* inID, const char* inData, uint32_t inColor)
{
}

void SuperLuminalEndScopedEvent()
{
}

#endif