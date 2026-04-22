// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEdGraph.h"
#include "Module/AnimNextModule_EditorData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextEdGraph)

void UAnimNextEdGraph::PostLoad()
{
	Super::PostLoad();

	UAnimNextRigVMAssetEditorData* EditorData = GetTypedOuter<UAnimNextRigVMAssetEditorData>();
	check(EditorData);
	Initialize(EditorData);
}

void UAnimNextEdGraph::Initialize(UAnimNextRigVMAssetEditorData* InEditorData)
{
	Schema = InEditorData->GetRigVMEdGraphSchemaClass();

	InEditorData->RigVMGraphModifiedEvent.RemoveAll(this);
	InEditorData->RigVMGraphModifiedEvent.AddUObject(this, &UAnimNextEdGraph::HandleModifiedEvent);
	InEditorData->RigVMCompiledEvent.RemoveAll(this);
	InEditorData->RigVMCompiledEvent.AddUObject(this, &UAnimNextEdGraph::HandleVMCompiledEvent);
}

FRigVMClient* UAnimNextEdGraph::GetRigVMClient() const
{
	if (const UAnimNextRigVMAssetEditorData* EditorData = GetTypedOuter<UAnimNextRigVMAssetEditorData>())
	{
		return const_cast<FRigVMClient*>(EditorData->GetRigVMClient());
	}
	return nullptr;
}
