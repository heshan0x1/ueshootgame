// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextTraceModule.h"

#if ANIMNEXT_TRACE_ENABLED
#include "AnimNextProvider.h"
#include "AnimNextAnalyzer.h"

FName FAnimNextTraceModule::ModuleName("UAF");

void FAnimNextTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("UAF");
}

void FAnimNextTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& InSession)
{
	TSharedPtr<FAnimNextProvider> AnimNextProvider = MakeShared<FAnimNextProvider>(InSession);
	InSession.AddProvider(FAnimNextProvider::ProviderName, AnimNextProvider);

	InSession.AddAnalyzer(new FAnimNextAnalyzer(InSession, *AnimNextProvider));
}

void FAnimNextTraceModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("UAF"));
}

void FAnimNextTraceModule::GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{

}
#endif //ANIMNEXT_TRACE_ENABLED
