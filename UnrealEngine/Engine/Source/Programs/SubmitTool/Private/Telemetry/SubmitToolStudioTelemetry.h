// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ITelemetry.h"

class FSubmitToolStudioTelemetry : public ITelemetry
{
public:
	FSubmitToolStudioTelemetry();
	virtual ~FSubmitToolStudioTelemetry();
	virtual void Start(const FString& InCurrentStream) const override;
	virtual void BlockFlush(float InTimeout) const override;
	virtual void SubmitSucceeded(TArray<FAnalyticsEventAttribute>&& InAttribs) const override;
	virtual void CustomEvent(const FString& InEventId, const TArray<FAnalyticsEventAttribute>& InAttribs) const override;
};