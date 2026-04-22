// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMorphTargetEditingToolInterface.h"

#include "ContextObjectStore.h"
#include "IMorphTargetEditingToolInterface.h"
#include "InteractiveToolManager.h"
#include "IPersonaEditorModeManager.h"
#include "MorphTargetEditingToolProperties.h"
#include "PersonaModule.h"
#include "SingleSelectionTool.h"
#include "SkeletalMesh/SkeletalMeshEditingInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IMorphTargetEditingToolInterface)

void IMorphTargetEditingToolInterface::SetupMorphEditingToolCommon()
{
	// Only support single selection tools at the moment
	USingleSelectionTool* SingleTargetTool = CastChecked<USingleSelectionTool>(this);
	ISkeletalMeshEditingInterface* EditingInterface = CastChecked<ISkeletalMeshEditingInterface>(this);
	USkeletalMeshEditorContextObjectBase* EditorContext = SingleTargetTool->GetToolManager()->GetContextObjectStore()->FindContext<USkeletalMeshEditorContextObjectBase>();
	if (EditorContext)
	{
		EditorContext->BindTo(EditingInterface);
	}
	
	auto SetupFunction = [&](UMorphTargetEditingToolProperties* InProperties)
	{
		InProperties->EditMorphTargetName = EditorContext->GetEditingMorphTarget();
	};
	
	SetupCommonProperties(SetupFunction);
}

void IMorphTargetEditingToolInterface::ShutdownMorphEditingToolCommon()
{
	ISkeletalMeshEditingInterface* EditingInterface = CastChecked<ISkeletalMeshEditingInterface>(this);

	// Only support single selection tools at the moment
	USingleSelectionTool* SingleTargetTool = CastChecked<USingleSelectionTool>(this);
	
	if (USkeletalMeshEditorContextObjectBase* EditorContext = SingleTargetTool->GetToolManager()->GetContextObjectStore()->FindContext<USkeletalMeshEditorContextObjectBase>())
	{
		EditorContext->UnbindFrom(EditingInterface);
	}	
}
