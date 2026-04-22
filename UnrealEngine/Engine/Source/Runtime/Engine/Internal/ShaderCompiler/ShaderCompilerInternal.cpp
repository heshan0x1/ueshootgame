// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerInternal.cpp: Implements FShaderCompileInternalUtilities.
=============================================================================*/

#include "ShaderCompilerInternal.h"
#include "ShaderCompiler.h"


void FShaderCompileInternalUtilities::ExecuteShaderCompileJob(FShaderCommonCompileJob& Job)
{
	FShaderCompileUtilities::ExecuteShaderCompileJob(Job);
}

void FShaderCompileInternalUtilities::EnableDumpDebugInfoForRetry(FShaderCommonCompileJob& Job)
{
	Job.ForEachSingleShaderJob(
		[](FShaderCompileJob& SingleJob)
		{
			if (SingleJob.Input.DumpDebugInfoPath.IsEmpty())
			{
				SingleJob.Input.DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(SingleJob.Input);
				// Any reissued jobs due to this condition will dump debug information, so increment the dump count here
				GShaderCompilingManager->IncrementNumDumpedShaderSources();
			}
		}
	);
}

void FShaderCompileInternalUtilities::DumpDebugInfo(FShaderCommonCompileJob& Job)
{
	FShaderCompileInternalUtilities::EnableDumpDebugInfoForRetry(Job);
	FShaderDebugDataContext Ctx;
	Job.OnComplete(Ctx);
}
