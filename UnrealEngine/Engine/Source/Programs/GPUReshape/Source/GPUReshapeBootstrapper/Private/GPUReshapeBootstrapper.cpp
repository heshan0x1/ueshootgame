// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "RequiredProgramMainCPPInclude.h"

DEFINE_LOG_CATEGORY_STATIC(LogGPUReshape, Log, All);

IMPLEMENT_APPLICATION(GPUReshapeBootstrapper, "GPUReshapeBootstrapper");

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	/**
	 * Gauntlet GPU Reshape Utility Helper
	 *
	 * Bootstrap Tree:
	 *   Gauntlet -> GPUReshapeBootstrapper -> GPUReshape -> Target
	 */
	
	FTaskTagScope Scope(ETaskTag::EGameThread);
	
	ON_SCOPE_EXIT
	{ 
		LLM(FLowLevelMemTracker::Get().UpdateStatsPerFrame());
		RequestEngineExit(TEXT("Exiting"));
		
		FEngineLoop::AppPreExit();
		FModuleManager::Get().UnloadModulesAtShutdown();
		FEngineLoop::AppExit();
	};

	if (int32 Ret = GEngineLoop.PreInit(ArgC, ArgV))
	{
		return Ret;
	}
	
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("WaitForDebugger")))
	{
		while (!FPlatformMisc::IsDebuggerPresent())
		{
			FPlatformProcess::Sleep(0.1f);
		}
		
		UE_DEBUG_BREAK();
	}
#endif // !UE_BUILD_SHIPPING

	FString BootstrapTarget;
	if (!FParse::Value(FCommandLine::Get(), TEXT("BootstrapTarget="), BootstrapTarget))
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Target executable path not set"));
		return 1u;
	}
	
	FString GPUReshapePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("GRS.Path="), GPUReshapePath))
	{
		UE_LOG(LogGPUReshape, Error, TEXT("GPU Reshape path not set"));
		return 1u;
	}
	
	FString WorkspacePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("GRS.Workspace="), WorkspacePath))
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Workspace path not set"));
		return 1u;
	}
	
	FString ReportPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("GRS.Report="), ReportPath))
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Report path not set"));
		return 1u;
	}
	
	int32 Timeout = 7200;
	FParse::Value(FCommandLine::Get(), TEXT("GRS.Timeout="), Timeout);
	
	FString SymbolPath = "";
	FParse::Value(FCommandLine::Get(), TEXT("GRS.SymbolPath="), SymbolPath);

	// If relative, put the report under the saved path
	if (FPaths::IsRelative(ReportPath))
	{
		ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), ReportPath);
	}
	
	// Start in headless mode
	FStringBuilderBase GRSCommandLine;
	GRSCommandLine << "launch ";
	GRSCommandLine << "-report " << "\"" << ReportPath << "\" ";
	GRSCommandLine << "-workspace \"" << WorkspacePath << "\" ";
	GRSCommandLine << "-timeout " << Timeout << " ";
	GRSCommandLine << "-symbol " << SymbolPath << " ";
	GRSCommandLine << "-app " << BootstrapTarget;

	for (int32_t i = 1; i < ArgC; i++)
	{
		GRSCommandLine << " " << ArgV[i];
	}

	// Create pipes
	void *PipeRead = nullptr, *PipeWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite))
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Failed to create redirect pipes"));
		return 1u;
	}
	
	// Launch the editor bootstrapped through reshape
	FProcHandle Handle = FPlatformProcess::CreateProc(
		*GPUReshapePath,
		GRSCommandLine.GetData(),
		true,
		false,
		false,
		nullptr,
		0,
		nullptr,
		PipeWrite
	);
	
	if (!Handle.IsValid())
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Failed to launch bootstrapped application"));
		return 1u;
	}

	// Wait for reshape to finish, redirect pipe meanwhile
	while (FPlatformProcess::IsProcRunning(Handle))
	{
		if (FString Contents = FPlatformProcess::ReadPipe(PipeRead); !Contents.IsEmpty())
		{
			UE_LOG(LogGPUReshape, Log, TEXT("%s"), *Contents);
		}
		
		FPlatformProcess::Sleep(0.1f);
	}

	// Redirect final contents
	if (FString Contents = FPlatformProcess::ReadPipe(PipeRead); !Contents.IsEmpty())
	{
		UE_LOG(LogGPUReshape, Log, TEXT("%s"), *Contents);
	}

	int32 ReturnCode = 1u;
	FPlatformProcess::GetProcReturnCode(Handle, &ReturnCode);
	FPlatformProcess::CloseProc(Handle);

	if (ReturnCode)
	{
		UE_LOG(LogGPUReshape, Error, TEXT("Bootstrapped process exited with %u"), ReturnCode);
	}
	
	return ReturnCode;
}
