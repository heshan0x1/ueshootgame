// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextExecuteContext.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AnimNextSharedVariables_EditorData.generated.h"

#define UE_API UAFUNCOOKEDONLY_API

class UAnimNextSharedVariablesFactory;

namespace UE::UAF::UncookedOnly
{
	struct FUtils;
}

/** Editor data for AnimNext data interfaces */
UCLASS(MinimalAPI)
class UAnimNextSharedVariables_EditorData : public UAnimNextRigVMAssetEditorData
{
	GENERATED_BODY()

protected:
	// UAnimNextRigVMAssetEditorData interface
	virtual UScriptStruct* GetExecuteContextStruct() const override { return FAnimNextExecuteContext::StaticStruct(); }
	UE_API virtual TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> GetEntryClasses() const override;
	UE_API virtual void CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const override;

	friend class UAnimNextSharedVariablesFactory;
	friend UE::UAF::UncookedOnly::FUtils;
};

#undef UE_API
