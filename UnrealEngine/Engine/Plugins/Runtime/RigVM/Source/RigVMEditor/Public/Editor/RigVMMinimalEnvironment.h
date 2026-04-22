// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMClient.h"
#include "EdGraph/RigVMEdGraph.h"
#include "EdGraph/RigVMEdGraphNode.h"
#include "UObject/StrongObjectPtr.h"

#define UE_API RIGVMEDITOR_API

class FRigVMMinimalEnvironment : public TSharedFromThis<FRigVMMinimalEnvironment>
{
public:
	UE_API FRigVMMinimalEnvironment(const UClass* InRigVMBlueprintClass = nullptr);

	UE_API URigVMGraph* GetModel() const;
	UE_API URigVMController* GetController() const;
	UE_API URigVMNode* GetNode() const;
	UE_API URigVMEdGraph* GetEdGraph() const;
	UE_API URigVMEdGraphNode* GetEdGraphNode() const;

	UE_API void SetSchemata(const UClass* InRigVMBlueprintClass);
	UE_API void SetNode(URigVMNode* InModelNode);
	UE_API void SetFunctionNode(const FRigVMGraphFunctionIdentifier& InIdentifier);
	
	UE_API FSimpleDelegate& OnChanged();
	UE_API void Tick_GameThead(float InDeltaTime);

private:

	UE_API void HandleModified(ERigVMGraphNotifType InNotification, URigVMGraph* InGraph, UObject* InSubject);
	
	TStrongObjectPtr<URigVMGraph> ModelGraph;
	TStrongObjectPtr<URigVMController> ModelController;
	UClass* EdGraphClass;
	UClass* EdGraphNodeClass;
	TStrongObjectPtr<URigVMEdGraph> EdGraph;
	TWeakObjectPtr<URigVMNode> ModelNode;
	TWeakObjectPtr<URigVMEdGraphNode> EdGraphNode;
	std::atomic<int32> NumModifications;
	FSimpleDelegate ChangedDelegate;
	FDelegateHandle ModelHandle;
};

#undef UE_API
