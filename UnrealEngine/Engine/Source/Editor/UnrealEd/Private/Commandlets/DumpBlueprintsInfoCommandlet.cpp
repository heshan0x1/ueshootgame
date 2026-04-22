// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/DumpBlueprintsInfoCommandlet.h"

#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"
#include "Trace/Detail/Channel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DumpBlueprintsInfoCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintInfoDump, Log, All);

/*******************************************************************************
 * UDumpBlueprintsInfoCommandlet
 ******************************************************************************/

//------------------------------------------------------------------------------
UDumpBlueprintsInfoCommandlet::UDumpBlueprintsInfoCommandlet(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//------------------------------------------------------------------------------
int32 UDumpBlueprintsInfoCommandlet::Main(FString const& Params)
{
	UE_LOG(LogBlueprintInfoDump, Error, TEXT("DumpBlueprintsInfo has been removed - consider using the GenerateBlueprintAPI commandlet instead\n"));
	return 0;
}

