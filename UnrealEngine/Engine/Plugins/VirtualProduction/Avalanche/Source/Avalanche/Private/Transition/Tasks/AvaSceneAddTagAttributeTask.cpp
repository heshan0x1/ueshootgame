// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transition/Tasks/AvaSceneAddTagAttributeTask.h"
#include "AvaAttributeContainer.h"
#include "IAvaSceneInterface.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "AvaSceneAddTagAttributeTask"

#if WITH_EDITOR
FText FAvaSceneAddTagAttributeTask::GetDescription(const FGuid& InId, FStateTreeDataView InInstanceDataView, const IStateTreeBindingLookup& InBindingLookup, EStateTreeNodeFormatting InFormatting) const
{
	const FInstanceDataType& InstanceData = InInstanceDataView.Get<FInstanceDataType>();

	const FText TagAttributeDesc = FText::FromName(InstanceData.TagAttribute.ToName());

	return InFormatting == EStateTreeNodeFormatting::RichText
		? FText::Format(LOCTEXT("DescRich", " <b>Add '{0}'</> <s>tag attribute to this scene</>"), TagAttributeDesc)
		: FText::Format(LOCTEXT("Desc", "Add '{0}' tag attribute to this scene"), TagAttributeDesc);
}
#endif

EStateTreeRunStatus FAvaSceneAddTagAttributeTask::EnterState(FStateTreeExecutionContext& InContext, const FStateTreeTransitionResult& InTransition) const
{
	IAvaSceneInterface* Scene = GetScene(InContext);
	if (!Scene)
	{
		return EStateTreeRunStatus::Failed;
	}

	UAvaAttributeContainer* AttributeContainer = Scene->GetAttributeContainer();
	if (!AttributeContainer)
	{
		return EStateTreeRunStatus::Failed;
	}

	const FInstanceDataType& InstanceData = InContext.GetInstanceData(*this);
	if (AttributeContainer->AddTagAttribute(InstanceData.TagAttribute))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Failed;
}

#undef LOCTEXT_NAMESPACE
