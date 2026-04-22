// Copyright Epic Games, Inc. All Rights Reserved.
#include "VersionInfoHandler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VersionInfoHandler)

UVersionInfoHandler* UVersionInfoHandler::Get()
{
	return GetMutableDefault <UVersionInfoHandler>();
};
