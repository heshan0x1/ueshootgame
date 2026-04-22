// Copyright Epic Games, Inc. All Rights Reserved.

#include "TelemetryService.h"

#include "Logging/SubmitToolLog.h"
#include "Parameters/SubmitToolParameters.h"
#include "NullTelemetry.h"
#include "StandAloneTelemetry.h"
#include "SubmitToolStudioTelemetry.h"

FCriticalSection FTelemetryService::InstanceCriticalSection;
TSharedPtr<ITelemetry> FTelemetryService::TelemetryInstance = nullptr;

const TSharedPtr<ITelemetry>& FTelemetryService::Get()
{
	return TelemetryInstance;
}

void FTelemetryService::Init(const FTelemetryParameters& InTelemetryParameters)
{
	if (InTelemetryParameters.Instance.Equals(TEXT("StandAlone"), ESearchCase::IgnoreCase))
	{
		if (InTelemetryParameters.Url.IsEmpty())
		{
			Set(MakeShared<FNullTelemetry>());
		}
		else
		{
			Set(MakeShared<FStandAloneTelemetry>(InTelemetryParameters.Url, FGuid()));
		}
	}
	else if (InTelemetryParameters.Instance.Equals(TEXT("StudioTelemetry"), ESearchCase::IgnoreCase))
	{
		Set(MakeShared<FSubmitToolStudioTelemetry>());
	}
	else
	{
		Set(MakeShared<FNullTelemetry>());
	}
}

void FTelemetryService::Set(TSharedPtr<ITelemetry> InInstance)
{
	if (InInstance == nullptr)
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Trying to set a null telemetry instance. Operation aborted."));
		return;
	}

	InstanceCriticalSection.Lock();
	
	TelemetryInstance = InInstance;

	InstanceCriticalSection.Unlock();
}

void FTelemetryService::BlockFlush(float InTimeout)
{
	TelemetryInstance->BlockFlush(InTimeout);
}

void FTelemetryService::Shutdown()
{
	TelemetryInstance = nullptr;
}
