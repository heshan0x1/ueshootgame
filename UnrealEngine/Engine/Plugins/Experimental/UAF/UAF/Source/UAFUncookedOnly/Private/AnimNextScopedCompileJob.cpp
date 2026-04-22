// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextScopedCompileJob.h"

#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "UncookedOnlyUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "AnimNextScopedCompileJob"

namespace UE::UAF::UncookedOnly
{

struct FCompileJobThreadData
{
	TArray<TWeakPtr<FCompilerResultsLog>> CurrentLogStack;
	TArray<TSharedRef<FTokenizedMessage>> Messages;
	TSet<TWeakObjectPtr<UAnimNextRigVMAsset>> CompiledAssets;
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	bool bDidCompile = false;
};

static thread_local FCompileJobThreadData GCompileJobData;

FScopedCompileJob::FScopedCompileJob(const FText& InJobName, TConstArrayView<UObject*> InAssets)
	: FScopedCompileJob(InJobName, nullptr, InAssets)
{
}

FScopedCompileJob::FScopedCompileJob(UObject* InObject)
	: FScopedCompileJob(FText::FromName(InObject->GetFName()), InObject, TArrayView<UObject*>(&InObject, 1))
{
}

FScopedCompileJob::FScopedCompileJob(const FText& InJobName, UObject* InObject, TConstArrayView<UObject*> InAssets)
	: JobName(InJobName)
	, Object(InObject)
{
	FCompileJobThreadData* ThreadData = &GCompileJobData;
	StartTime = FPlatformTime::Seconds();
	ThreadData->bDidCompile = Object != nullptr;
	Log = MakeShared<FCompilerResultsLog>(); 
	ThreadData->CurrentLogStack.Push(Log);

	for(UObject* Asset : InAssets)
	{
		UAnimNextRigVMAsset* AnimNextRigVMAsset = Cast<UAnimNextRigVMAsset>(Asset);
		if(AnimNextRigVMAsset == nullptr)
		{
			return;
		}

		UAnimNextRigVMAssetEditorData* EditorData = FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(AnimNextRigVMAsset);
		if(EditorData == nullptr)
		{
			return;
		}

		bool bAlreadyCompiling = false;
		ThreadData->CompiledAssets.Add(AnimNextRigVMAsset, &bAlreadyCompiling);

		if (!bAlreadyCompiling)
		{
			EditorData->ClearErrorInfoForAllEdGraphs();
		}

		if (!bAlreadyCompiling && !EditorData->bSuspendCompilationNotifications)
		{
			EditorData->OnCompileJobStarted();
			UAnimNextRigVMAsset::OnCompileJobStarted().Broadcast(AnimNextRigVMAsset);
		}
	}
}

FScopedCompileJob::~FScopedCompileJob()
{
	FinishTime = FPlatformTime::Seconds();

	FCompileJobThreadData* ThreadData = &GCompileJobData;
	
	// Accumulate messages
	ThreadData->Messages.Append(Log->Messages);
	ThreadData->NumErrors += Log->NumErrors;
	ThreadData->NumWarnings += Log->NumWarnings;

	ThreadData->CurrentLogStack.Pop();

	if(ThreadData->CurrentLogStack.IsEmpty())
	{
		if(ThreadData->bDidCompile)
		{
			FMessageLog MessageLog("AnimNextCompilerResults");

			MessageLog.NewPage(FText::Format(LOCTEXT("CompileFormat", "Compile {0}: {1}"), JobName, FText::AsDateTime(FDateTime::UtcNow())));
			MessageLog.AddMessages(ThreadData->Messages);

			// Print summary
			FNumberFormattingOptions TimeFormat;
			TimeFormat.MaximumFractionalDigits = 2;
			TimeFormat.MinimumFractionalDigits = 2;
			TimeFormat.MaximumIntegralDigits = 4;
			TimeFormat.MinimumIntegralDigits = 4;
			TimeFormat.UseGrouping = false;

			FFormatNamedArguments Args;
			Args.Add(TEXT("CurrentTime"), FText::AsNumber(FinishTime - GStartTime, &TimeFormat));
			Args.Add(TEXT("JobName"), JobName);
			Args.Add(TEXT("CompileTime"), (int32)((FinishTime - StartTime) * 1000));
			Args.Add(TEXT("ObjectPath"), Object != nullptr ? FText::Format(LOCTEXT("ObjectPathFormat", "({0})"), FText::FromString(Object->GetPathName())) : FText::GetEmpty());
			Args.Add(TEXT("NumAssets"), ThreadData->CompiledAssets.Num());

			if (ThreadData->NumErrors > 0)
			{
				Args.Add(TEXT("NumErrors"), ThreadData->NumErrors);
				Args.Add(TEXT("NumWarnings"), ThreadData->NumWarnings);
				MessageLog.Info(FText::Format(LOCTEXT("CompileFailed", "[{CurrentTime}] Compile of {JobName} failed. {NumErrors} Error(s), {NumWarnings} Warning(s) [{NumAssets} assets in {CompileTime} ms] {ObjectPath}"), MoveTemp(Args)));
			}
			else if(ThreadData->NumWarnings > 0)
			{
				Args.Add(TEXT("NumWarnings"), ThreadData->NumWarnings);
				MessageLog.Info(FText::Format(LOCTEXT("CompileWarning", "[{CurrentTime}] Compile of {JobName} successful. {NumWarnings} Warning(s) [{NumAssets} assets in {CompileTime} ms] {ObjectPath}"), MoveTemp(Args)));
			}
			else
			{
				MessageLog.Info(FText::Format(LOCTEXT("CompileSuccess", "[{CurrentTime}] Compile of {JobName} successful! [{NumAssets} assets in {CompileTime} ms] {ObjectPath}"), MoveTemp(Args)));
			}

			// Broadcast compilation finished so reallocation can occur
			for (TWeakObjectPtr<UAnimNextRigVMAsset> WeakAsset : ThreadData->CompiledAssets)
			{
				UAnimNextRigVMAsset* Asset = WeakAsset.Get();
				if (Asset == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				if (EditorData == nullptr)
				{
					continue;
				}

				Asset->CompilationState = [&EditorData]()
				{
					if (EditorData->bErrorsDuringCompilation)
					{
						return EAnimNextRigVMAssetState::CompiledWithErrors;
					}

					if (EditorData->bWarningsDuringCompilation)
					{
						return EAnimNextRigVMAssetState::CompiledWithWarnings;
					}

					return EAnimNextRigVMAssetState::CompiledWithSuccess;
				}();			

				if (!EditorData->bSuspendCompilationNotifications)
				{
					UAnimNextRigVMAsset::OnCompileJobFinished().Broadcast(Asset);
					EditorData->OnCompileJobFinished();
				}
			}
		}

		ThreadData->CompiledAssets.Empty();
		ThreadData->Messages.Empty();
		ThreadData->NumErrors = 0;
		ThreadData->NumWarnings = 0;
		ThreadData->bDidCompile = false;
	}
}

FCompilerResultsLog& FScopedCompileJob::GetLog()
{
	FCompileJobThreadData* ThreadData = &GCompileJobData;
	check(ThreadData->CurrentLogStack.Num() && ThreadData->CurrentLogStack.Top().IsValid());
	return *ThreadData->CurrentLogStack.Top().Pin().Get();
}

}

#undef LOCTEXT_NAMESPACE