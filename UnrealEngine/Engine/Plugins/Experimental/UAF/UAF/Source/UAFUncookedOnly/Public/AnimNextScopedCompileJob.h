// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet2/CompilerResultsLog.h"

class UAnimNextRigVMAssetEditorData;
class FCompilerResultsLog;

namespace UE::UAF::UncookedOnly
{

// RAII helper for scoping compilation batches
// The re-allocation of compiled assets and their dependencies is deferred until the outermost scope exits.
class FScopedCompileJob
{
public:
	UAFUNCOOKEDONLY_API explicit FScopedCompileJob(const FText& InJobName, TConstArrayView<UObject*> InAssets);
	UAFUNCOOKEDONLY_API explicit FScopedCompileJob(UObject* InObject);
	UAFUNCOOKEDONLY_API ~FScopedCompileJob();

	// Get the log that is currently in scope, asserts if no scope is active
	UAFUNCOOKEDONLY_API static FCompilerResultsLog& GetLog();

private:
	UAFUNCOOKEDONLY_API FScopedCompileJob(const FText& InJobName, UObject* InObject, TConstArrayView<UObject*> InAssets);

private:
	TSharedPtr<FCompilerResultsLog> Log;
	FText JobName;
	UObject* Object = nullptr;
	double StartTime = 0.0;
	double FinishTime = 0.0;
};

}
