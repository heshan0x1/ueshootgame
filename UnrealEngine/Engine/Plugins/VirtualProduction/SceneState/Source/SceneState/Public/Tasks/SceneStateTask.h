// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyBindingTypes.h"
#include "SceneStateEnums.h"
#include "SceneStateRange.h"
#include "SceneStateTask.generated.h"

struct FSceneStateBindingDesc;
struct FSceneStateExecutionContext;
struct FSceneStateTaskBindingExtension;
struct FSceneStateTaskInstance;

namespace UE::SceneState
{
	struct FTaskEditChange;

	namespace Editor
	{
		class FBindingCompiler;
		class FBlueprintCompilerContext;
		class FStateMachineCompiler;
		class FStateMachineTaskCompiler;
	}
}

/**
 * Base class for Tasks.
 * Tasks are immutable in execution time, and so are meant to only hold logic and template read-only data.
 * Each Task has a Task Instance Type that is allocated and used for instance data that the task can then mutate.
 * @see FSceneStateTaskInstance
 */
USTRUCT(meta=(Hidden))
struct FSceneStateTask
{
	GENERATED_BODY()

	virtual ~FSceneStateTask() = default;

#if WITH_EDITOR
	/**
	 * Called in-editor to get the task instance type
	 * @return the task instance struct. Must derive from FSceneStateTaskInstance
	 */
	SCENESTATE_API const UScriptStruct* GetTaskInstanceType() const;

	/**
	 * Called to init properties (e.g. Guids) or instances objects within the task instance
	 * @param InOuter the outer to use for instanced objects
	 * @param InTaskInstance the task instance to build
	 */
	SCENESTATE_API void BuildTaskInstance(UObject* InOuter, FStructView InTaskInstance) const;

	/**
	 * Called when a property within a task or task instance has changed
	 * @param InEditChange the edit change information (property changed event, outer)
	 * @param InTaskInstance the task instance paired with the task
	 */
	SCENESTATE_API void PostEditChange(UE::SceneState::FTaskEditChange& InEditChange, FStructView InTaskInstance);
#endif

	/** Called to get the optional Binding Extension for a Task */
	SCENESTATE_API const FSceneStateTaskBindingExtension* GetBindingExtension() const;

	/** Finds the Task Instance for the given Execution Instance */
	SCENESTATE_API FStructView FindTaskInstance(const FSceneStateExecutionContext& InContext) const;

	/**
	 * Called when the State holding the Task first starts.
	 * This is called for all tasks held by the state even if these tasks end up not running, or run at a later time.
	 */
	SCENESTATE_API void Setup(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const;

	/**
	 * Called once the State processes that the Task has all its prerequisites met.
	 * Applies and property bindings to the task and calls OnStart().
	 */
	SCENESTATE_API void Start(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const;

	/**
	 * Called by the State each Tick.
	 * OnTick() gets called if the Task is running and set to Tickable (see TaskFlags)
	 */
	SCENESTATE_API void Tick(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance, float InDeltaSeconds) const;

	/**
	 * Called to Stop the Task, because it either finished or was forcibly stopped by the state
	 * @see ESceneStateTaskStopReason
	 */
	SCENESTATE_API void Stop(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance, ESceneStateTaskStopReason InStopReason) const;

	/** Calls Stop with stop reason being that the task has finished */
	SCENESTATE_API void Finish(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const;

	uint16 GetParentStateIndex() const
	{
		return ParentStateIndex;
	}

	uint16 GetTaskIndex() const
	{
		return TaskIndex;
	}

	FSceneStateRange GetPrerequisiteRange() const
	{
		return PrerequisiteRange;
	}

protected:
	SCENESTATE_API void SetFlags(ESceneStateTaskFlags InFlags);

	SCENESTATE_API void ClearFlags(ESceneStateTaskFlags InFlags);

#if WITH_EDITOR
	/**
	 * Called in-editor to get the task instance type
	 * @see GetTaskInstanceType
	 * @return the task instance struct. Must derive from FSceneStateTaskInstance
	 */
	virtual const UScriptStruct* OnGetTaskInstanceType() const
	{
		return nullptr;
	}

	/**
	 * Called to init properties (e.g. Guids) or instances objects within the task instance
	 * @see BuildTaskInstance
	 */
	virtual void OnBuildTaskInstance(UObject* InOuter, FStructView InTaskInstance) const
	{
	}

	/**
	 * Called when a property within a task has changed
	 * @see PostEditChange
	 */
	virtual void OnPostEditChange(UE::SceneState::FTaskEditChange& InEditChange, FStructView InTaskInstance) const
	{
	}
#endif

	/**
	 * Called to get the optional Binding Extension for a Task
	 * @see GetBindingExtension
	 */
	virtual const FSceneStateTaskBindingExtension* OnGetBindingExtension() const
	{
		return nullptr;
	}

	/**
	 * Called when the State holding the Task first starts.
	 * This is called for all tasks held by the state even if these tasks end up not running, or run at a later time.
	 */
	virtual void OnSetup(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const
	{
	}

	/** Called once the State processes that the Task has all its prerequisites met. */
	virtual void OnStart(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const
	{
	}

	/** Called by the State each Tick if the Task is set to Tick via its TaskFlags */
	virtual void OnTick(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance, float InDeltaSeconds) const
	{
	}

	/**
	 * Called when the Task has finished or forcibly stopped by the state
	 * @see ESceneStateTaskStopReason
	 */
	virtual void OnStop(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance, ESceneStateTaskStopReason InStopReason) const
	{
	}

private:
	/** Applies Bindings to the given Task Instance */
	bool ApplyBindings(const FSceneStateExecutionContext& InContext, FStructView InTaskInstance) const;

	/** Bindings Batch where this Task is target */
	UPROPERTY()
	FPropertyBindingIndex16 BindingsBatch;

	/** Absolute Index to the State owning this Task */
	UPROPERTY()
	uint16 ParentStateIndex = FSceneStateRange::InvalidIndex;

	/** Absolute Index of this Task */
	UPROPERTY()
	uint16 TaskIndex = FSceneStateRange::InvalidIndex;

	/** Absolute Range to the relative indices of the tasks that need to finish before this task can be executed */
	UPROPERTY()
	FSceneStateRange PrerequisiteRange;

	/** Additional Information about how this Task (e.g. how it Ticks, etc) */
	ESceneStateTaskFlags TaskFlags = ESceneStateTaskFlags::None;

	friend UE::SceneState::Editor::FBindingCompiler;
	friend UE::SceneState::Editor::FBlueprintCompilerContext;
	friend UE::SceneState::Editor::FStateMachineCompiler;
	friend UE::SceneState::Editor::FStateMachineTaskCompiler;
};
