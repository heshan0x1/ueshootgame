// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneStateMachineNode.h"
#include "StructUtils/InstancedStruct.h"
#include "Tasks/SceneStateTask.h"
#include "Templates/SubclassOf.h"
#include "SceneStateMachineTaskNode.generated.h"

class USceneStateBlueprintableTask;
struct FSceneStateTaskDescContext;
struct FSceneStateTaskDescMutableContext;

namespace UE::SceneState
{
	enum class ETaskObjectType : uint8;
	struct FStructIdChange;
}

UCLASS(MinimalAPI)
class USceneStateMachineTaskNode : public USceneStateMachineNode
{
	GENERATED_BODY()

public:
	USceneStateMachineTaskNode();

	TConstStructView<FSceneStateTask> GetTask() const
	{
		return Task;
	}

	TStructView<FSceneStateTaskInstance> GetTaskInstance()
	{
		return TaskInstance;
	}

	TMulticastDelegateRegistration<void()>& OnPostEditTask() const
	{
		return OnPostEditTaskDelegate;
	}

	SCENESTATEMACHINEGRAPH_API bool SetTaskStruct(const UScriptStruct* InTaskStruct);

	SCENESTATEMACHINEGRAPH_API void SetTaskBlueprintClass(TSubclassOf<USceneStateBlueprintableTask> InTaskClass);

	SCENESTATEMACHINEGRAPH_API bool FindDataViewById(const FGuid& InStructId, FStructView& OutDataView);

	const FGuid& GetTaskId() const
	{
		return TaskId;
	}

	//~ Begin UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void OnRenameNode(const FString& InNodeName);
	virtual FText GetNodeTitle(ENodeTitleType::Type InTitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanDuplicateNode() const override;
	virtual void PostPasteNode() override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual TSharedPtr<INameValidatorInterface> MakeNameValidator() const override;
	//~ End UEdGraphNode

	//~ Begin UObject
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent) override;
	virtual void PostDuplicate(bool bInDuplicateForPIE) override;
	virtual void PostEditImport() override;
	//~ End UObject

	static FName GetTaskPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(USceneStateMachineTaskNode, Task);
	}

	static FName GetTaskInstancePropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(USceneStateMachineTaskNode, TaskInstance);
	}

private:
	/** Determines whether the changed property comes from the task or task instance object, or none of them */
	static UE::SceneState::ETaskObjectType GetChangedObject(FPropertyChangedChainEvent& InPropertyChangedEvent);

	/** Called on PostEditChangeChainProperty when the property that changed came from a Task or task instance object */
	void PostEditChangeTask(FPropertyChangedChainEvent& InPropertyChangedEvent, UE::SceneState::ETaskObjectType InChangedObject);

	/** Create context for this task node */
	FSceneStateTaskDescContext MakeContext() const;

	/** Creates a mutable context for this task node */
	FSceneStateTaskDescMutableContext MakeMutableContext();

	/** Rebuilds the task instance by recreating any instanced objects / or re-initializing any 'instance' guid property */
	void RebuildTaskInstance();

	/** Called when the task has changed its type, or a property related to the task / task instance has changed. */
	void OnTaskChanged();

	/** Called when a struct id has changed */
	void OnStructIdChanged(const UE::SceneState::FStructIdChange& InChange);

	/** Generates a new task id for this task node, notifying the binding owner of a struct id change */
	void GenerateNewTaskId();

	UPROPERTY(EditAnywhere, Category = "Task", meta=(StructTypeConst))
	TInstancedStruct<FSceneStateTask> Task;

	UPROPERTY(EditAnywhere, Category="Task", meta=(StructTypeConst))
	TInstancedStruct<FSceneStateTaskInstance> TaskInstance;

	UPROPERTY()
	FGuid TaskId;

	mutable TMulticastDelegate<void()> OnPostEditTaskDelegate;

	mutable FText CachedTaskDisplayName;
};
