// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeFunctionLibraryHelper.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "UncookedOnlyUtils.h"

const TArray<FName> UAnimNextStateTreeFunctionLibraryHelper::GetExposedAnimNextFunctionNames()
{
	// Can't cache here since the list of functions is dynamic.
	TArray<FName> Result;

	TMap<FAssetData, FRigVMGraphFunctionHeaderArray> FunctionExports;
	UE::UAF::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::UAF::AnimNextPublicGraphFunctionsExportsRegistryTag, FunctionExports);
	UE::UAF::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::UAF::ControlRigAssetPublicGraphFunctionsExportsRegistryTag, FunctionExports);
	
	for (const TPair<FAssetData, FRigVMGraphFunctionHeaderArray>& Export : FunctionExports.Array())
	{
		for (const FRigVMGraphFunctionHeader& FunctionHeader : Export.Value.Headers)
		{
			Result.Add(FunctionHeader.Name);
		}
	}

	return Result;
}