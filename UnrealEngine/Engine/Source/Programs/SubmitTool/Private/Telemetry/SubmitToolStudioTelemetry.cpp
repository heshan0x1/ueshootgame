// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolStudioTelemetry.h"
#include "Logging/SubmitToolLog.h"
#include "Version/AppVersion.h"
#include "AnalyticsEventAttribute.h"
#include "StudioTelemetry.h"

const FName SpanId = TEXT("SubmitTool.Session");

FSubmitToolStudioTelemetry::FSubmitToolStudioTelemetry()
{
	FStudioTelemetry::Get().StartSession();	
	FStudioTelemetry::Get().StartSpan(SpanId);
}

FSubmitToolStudioTelemetry::~FSubmitToolStudioTelemetry()
{
	FStudioTelemetry::Get().EndSpan(SpanId);
	FStudioTelemetry::Get().EndSession();
}

void FSubmitToolStudioTelemetry::Start(const FString& InCurrentStream) const
{
	FStudioTelemetry::Get().RecordEvent(
		TEXT("SubmitTool.StandAlone.Start"),
		MakeAnalyticsEventAttributeArray(
			TEXT("Version"), FAppVersion::GetVersion(),
			TEXT("Stream"), InCurrentStream
		)
	);
}

void FSubmitToolStudioTelemetry::BlockFlush(float InTimeout) const
{
	FStudioTelemetry::Get().FlushEvents();
}
void FSubmitToolStudioTelemetry::CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const
{
	FStudioTelemetry::Get().RecordEvent(InEventId, InAttribs);
}

void FSubmitToolStudioTelemetry::SubmitSucceeded(TArray<FAnalyticsEventAttribute>&& InAttribs) const
{
	FStudioTelemetry::Get().RecordEvent(
		TEXT("SubmitTool.StandAlone.Submit.Succeeded"),
		AppendAnalyticsEventAttributeArray(
			InAttribs,
			TEXT("Version"), FAppVersion::GetVersion()
		)
	);
}