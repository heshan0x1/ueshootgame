// Copyright Epic Games, Inc. All Rights Reserved.
#include "PerformanceCaptureWorkflowRuntime.h"

#define LOCTEXT_NAMESPACE "FPerformanceCaptureWorkflowRuntimeModule"

DEFINE_LOG_CATEGORY(LogPCapRuntime);

void FPerformanceCaptureWorkflowRuntimeModule::StartupModule()
{
	
}

void FPerformanceCaptureWorkflowRuntimeModule::ShutdownModule()
{
    
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FPerformanceCaptureWorkflowRuntimeModule, PerformanceCaptureWorkflowRuntime)