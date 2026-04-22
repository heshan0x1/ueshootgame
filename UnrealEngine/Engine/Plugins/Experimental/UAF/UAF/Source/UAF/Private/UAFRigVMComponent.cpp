// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFRigVMComponent.h"

#include "AnimNextRigVMAsset.h"
#include "UAFAssetInstance.h"
#include "Variables/UAFInstanceVariableContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFRigVMComponent)

FUAFRigVMComponent::FUAFRigVMComponent()
{
	FUAFAssetInstance* AssetInstance = GetAssetInstancePtr();
	if (AssetInstance == nullptr)
	{
		return;
	}

	const UAnimNextRigVMAsset* Asset = AssetInstance->GetAsset<UAnimNextRigVMAsset>();
	URigVM* VM = Asset->RigVM;
	if (VM == nullptr)
	{
		return;
	}

	// Initialize the RigVM context
	ExtendedExecuteContext = Asset->GetRigVMExtendedExecuteContext();

	// Hookup the runtime data ptrs
	const TArray<FRigVMExternalVariableDef>& ExternalVariableDefs = VM->GetExternalVariableDefs();
	const int32 NumExternalVariables = ExternalVariableDefs.Num();
	check(AssetInstance->Variables.NumInternalVariables == NumExternalVariables);
	if(NumExternalVariables > 0)
	{
		TArray<FRigVMExternalVariableRuntimeData> ExternalVariableRuntimeData;
		ExternalVariableRuntimeData.Reserve(AssetInstance->Variables.NumInternalVariables);
		const int32 NumInternalSharedVariableContainers = AssetInstance->Variables.InternalSharedVariableContainers.Num();
		int32 CurrentBaseIndex = 0;
		for (int32 SharedVariableSetIndex = 0; SharedVariableSetIndex < NumInternalSharedVariableContainers; ++SharedVariableSetIndex)
		{
			const TSharedRef<FUAFInstanceVariableContainer>& SharedVariableSet = AssetInstance->Variables.InternalSharedVariableContainers[SharedVariableSetIndex].Pin().ToSharedRef();
			TConstArrayView<FRigVMExternalVariableDef> DefsForSet(ExternalVariableDefs.GetData() + CurrentBaseIndex, SharedVariableSet->NumVariables);
			SharedVariableSet->GetRigVMMemoryForVariables(DefsForSet, ExternalVariableRuntimeData);
			CurrentBaseIndex += SharedVariableSet->NumVariables;
		}
		ExtendedExecuteContext.ExternalVariableRuntimeData = MoveTemp(ExternalVariableRuntimeData);
	}

	// Now initialize the 'instance', cache memory handles etc. in the context
	VM->InitializeInstance(ExtendedExecuteContext);
}
