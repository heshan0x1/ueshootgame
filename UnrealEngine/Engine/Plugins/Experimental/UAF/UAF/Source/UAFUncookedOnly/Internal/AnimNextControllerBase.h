// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMController.h"
#include "AnimNextControllerBase.generated.h"

class UAnimNextSharedVariables;
class UAnimNextSharedVariableNode;
class UAnimNextRigVMAssetEditorData;

/**
  * Implements AnimNext RigVM controller extensions
  */
UCLASS(MinimalAPI)
class UAnimNextControllerBase : public URigVMController
{
	GENERATED_BODY()

public:
	// Add a shared variable from the specified asset
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UAFUNCOOKEDONLY_API UAnimNextSharedVariableNode* AddAssetSharedVariableNode(const UAnimNextSharedVariables* InAsset, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true, bool bPrintPythonCommand = false);

	// Add a shared variable from the specified struct
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UAFUNCOOKEDONLY_API UAnimNextSharedVariableNode* AddStructSharedVariableNode(const UScriptStruct* InStruct, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition = FVector2D::ZeroVector, const FString& InNodeName = TEXT(""), bool bSetupUndoRedo = true, bool bPrintPythonCommand = false);

	// Refreshes a UAnimNextSharedVariableNode instance with provided data (similar to URigVMController::RefreshVariableNode)
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UAFUNCOOKEDONLY_API void RefreshSharedVariableNode(const FName& InNodeName, const FString& InSourceObjectPath, const FName& InVariableName, const FString& InCPPType, UObject* InCPPTypeObject, bool bSetupUndoRedo, bool bPrintPythonCommand);

	// Add a shared variable from the specified source object
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UAFUNCOOKEDONLY_API UAnimNextSharedVariableNode* AddSharedVariableNode(const FString& InSourceObjectPath, const FName& InVariableName, const FString& InCPPType, const FString& InCPPTypeObjectPath, bool bIsGetter, const FString& InDefaultValue, const FVector2D& InPosition, const FString& InNodeName, bool bSetupUndoRedo, bool bPrintPythonCommand);
	
	UAnimNextSharedVariableNode* ReplaceVariableNodeWithSharedVariableNode(URigVMVariableNode* InVariableNode, FName InNewVariableName, const UObject* InAssetOrStruct, bool bSetupUndoRedo, bool bPrintPythonCommand);
	URigVMVariableNode* ReplaceSharedVariableNodeWithVariableNode(UAnimNextSharedVariableNode* InVariableNode, FName InNewVariableName, bool bSetupUndoRedo, bool bPrintPythonCommand);
};
