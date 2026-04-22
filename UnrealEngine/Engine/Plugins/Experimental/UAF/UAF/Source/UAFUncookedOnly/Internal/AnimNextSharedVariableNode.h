// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Entries/AnimNextSharedVariablesEntry.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "AnimNextSharedVariableNode.generated.h"

class UAnimNextSharedVariables;
class UAnimNextControllerBase;

namespace UE::UAF::UncookedOnly
{
struct FUtils;
}

namespace UE::UAF::Editor
{
class FAnimNextEditorModule;
}

UCLASS()
class UAnimNextSharedVariableNode : public URigVMVariableNode
{
	GENERATED_BODY()

	// URigVMVariableNode interface
	virtual FString GetNodeSubTitle() const override;

private:
	friend UAnimNextControllerBase;
	friend UE::UAF::UncookedOnly::FUtils;
	friend UE::UAF::Editor::FAnimNextEditorModule;

	UPROPERTY()
	TObjectPtr<const UAnimNextSharedVariables> Asset;

	UPROPERTY()
	TObjectPtr<const UScriptStruct> Struct;
	
	UPROPERTY()
	EAnimNextSharedVariablesType Type;
};