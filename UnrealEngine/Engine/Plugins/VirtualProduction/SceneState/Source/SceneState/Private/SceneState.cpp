// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneState.h"
#include "SceneStateEventHandler.h"
#include "SceneStateEventStream.h"
#include "SceneStateExecutionContext.h"
#include "SceneStateInstance.h"
#include "SceneStateLog.h"
#include "SceneStateMachine.h"
#include "SceneStateObject.h"
#include "SceneStateUtils.h"
#include "StructUtils/InstancedStructContainer.h"
#include "Tasks/SceneStateTask.h"
#include "Tasks/SceneStateTaskInstance.h"
#include "Transition/SceneStateTransition.h"

void FSceneState::Enter(const FSceneStateExecutionContext& InContext) const
{
	using namespace UE::SceneState;

	const FReentryGuard ReentryGuard(ReentryHandle, InContext);
	if (ReentryGuard.IsReentry())
	{
		return;
	}

	// Enter State, add a State Instance if not already present
	FSceneStateInstance* Instance = InContext.FindOrAddStateInstance(*this);
	if (!Instance || Instance->Status == EExecutionStatus::Running)
	{
		return;
	}

	UE_LOG(LogSceneState, Verbose, TEXT("State (%s) receiving enter"), GetStateName(InContext).GetData());

	Instance->ElapsedTime = 0.f;
	Instance->Status = EExecutionStatus::Running;

	// Apply Event Handlers before anything else starts so the Event Data becomes available to substate machines and tasks
	CaptureEvents(InContext);
	AllocateTaskInstances(InContext, InContext.GetTemplateTaskInstances(*this));

	for (const FSceneStateTransition& Transition : InContext.GetTransitions(*this))
	{
		Transition.Setup(InContext);
	}

	for (const FSceneStateMachine& StateMachine : InContext.GetStateMachines(*this))
	{
		StateMachine.Setup(InContext);
	}

	// Setup each Task
	InContext.ForEachTask(*this,
		[&InContext, Instance](const FSceneStateTask& InTask, FStructView InTaskInstance)->EIterationResult
		{
			InTask.Setup(InContext, InTaskInstance);
			return EIterationResult::Continue;
		});

	for (const FSceneStateMachine& StateMachine : InContext.GetStateMachines(*this))
	{
		StateMachine.Start(InContext);
	}

	UpdateActiveTasks(InContext, *Instance);
}

void FSceneState::Tick(const FSceneStateExecutionContext& InContext, float InDeltaSeconds) const
{
	using namespace UE::SceneState;

	const FReentryGuard ReentryGuard(ReentryHandle, InContext);
	if (ReentryGuard.IsReentry())
	{
		return;
	}

	FSceneStateInstance* Instance = InContext.FindStateInstance(*this);
	if (!Instance || Instance->Status != EExecutionStatus::Running)
	{
		return;
	}

	UE_LOG(LogSceneState, VeryVerbose, TEXT("State (%s) receiving tick"), GetStateName(InContext).GetData());

	Instance->ElapsedTime += InDeltaSeconds;

	UpdateActiveTasks(InContext, *Instance);

	for (const FSceneStateMachine& StateMachine : InContext.GetStateMachines(*this))
	{
		StateMachine.Tick(InContext, InDeltaSeconds);
	}

	InContext.ForEachTask(*this,
		[&InContext, Instance, InDeltaSeconds](const FSceneStateTask& InTask, FStructView InTaskInstance)
		{
			InTask.Tick(InContext, InTaskInstance, InDeltaSeconds);
			return EIterationResult::Continue;
		});
}

void FSceneState::Exit(const FSceneStateExecutionContext& InContext) const
{
	using namespace UE::SceneState;

	const FReentryGuard ReentryGuard(ReentryHandle, InContext);
	if (ReentryGuard.IsReentry())
	{
		return;
	}

	FSceneStateInstance* Instance = InContext.FindStateInstance(*this);
	if (!Instance || Instance->Status != EExecutionStatus::Running)
	{
		return;
	}

	UE_LOG(LogSceneState, Verbose, TEXT("State (%s) receiving exit"), GetStateName(InContext).GetData());

	// Stop State Machines that are still running
	for (const FSceneStateMachine& StateMachine : InContext.GetStateMachines(*this))
	{
		StateMachine.Stop(InContext);
	}

	// Stop tasks that are still running
	InContext.ForEachTask(*this,
		[&InContext, Instance](const FSceneStateTask& InTask, FStructView InTaskInstance)
		{
			InTask.Stop(InContext, InTaskInstance, ESceneStateTaskStopReason::State);
			return EIterationResult::Continue;
		});

	// Notify transitions of stop
	for (const FSceneStateTransition& Transition : InContext.GetTransitions(*this))
	{
		Transition.Exit(InContext);
	}

	Instance->Status = EExecutionStatus::Finished;
	Instance->ElapsedTime = 0.f;

	InContext.RemoveStateInstance(*this);
	InContext.RemoveTaskInstanceContainer(*this);

	ResetCapturedEvents(InContext);
}

void FSceneState::UpdateActiveTasks(const FSceneStateExecutionContext& InContext, FSceneStateInstance& InInstance) const
{
	using namespace UE::SceneState;

	FInstancedStructContainer* TaskInstanceContainer = InContext.FindTaskInstanceContainer(*this);
	if (!TaskInstanceContainer)
	{
		return;
	}

	auto FindTaskInstance = 
		[TaskInstanceContainer](uint16 InRelativeIndex)->FSceneStateTaskInstance*
		{
			if (TaskInstanceContainer->IsValidIndex(InRelativeIndex))
			{
				return (*TaskInstanceContainer)[InRelativeIndex].GetPtr<FSceneStateTaskInstance>();
			}
			return nullptr;
		};

	// Start all the Tasks that haven't started yet and that meet their pre-requisites
	InContext.ForEachTask(*this,
		[&InContext, &InInstance, &FindTaskInstance](const FSceneStateTask& InTask, FStructView InTaskInstance)->EIterationResult
		{
			FSceneStateTaskInstance* TaskInstance = InTaskInstance.GetPtr<FSceneStateTaskInstance>();
			if (!TaskInstance || TaskInstance->GetStatus() != EExecutionStatus::NotStarted)
			{
				return EIterationResult::Continue;
			}

			bool bPrerequisitesMet = true;
			for (uint16 Prerequisite : InContext.GetTaskPrerequisites(InTask))
			{
				FSceneStateTaskInstance* const PrerequisiteTaskInstance = FindTaskInstance(Prerequisite);
				if (!PrerequisiteTaskInstance || PrerequisiteTaskInstance->GetStatus() != EExecutionStatus::Finished)
				{
					bPrerequisitesMet = false;
					break;
				}
			}

			if (bPrerequisitesMet)
			{
				InTask.Start(InContext, InTaskInstance);
			}

			return EIterationResult::Continue;
		});
}

FStringView FSceneState::GetStateName(const FSceneStateExecutionContext& InContext) const
{
#if WITH_EDITOR
	if (const FSceneStateMetadata* StateMetadata = InContext.GetStateMetadata(*this))
	{
		return StateMetadata->StateName;
	}
#endif
	return FStringView();
}

bool FSceneState::HasPendingTasks(const FSceneStateExecutionContext& InContext) const
{
	using namespace UE::SceneState;

	FSceneStateInstance* const StateInstance = InContext.FindStateInstance(*this);
	if (!StateInstance)
	{
		return false;
	}

	bool bHasPendingTask = false;

	InContext.ForEachTask(*this,
		[StateInstance, &bHasPendingTask](const FSceneStateTask& InTask, FStructView InTaskInstance)->EIterationResult
		{
			FSceneStateTaskInstance* TaskInstance = InTaskInstance.GetPtr<FSceneStateTaskInstance>();
			if (TaskInstance && TaskInstance->GetStatus() != EExecutionStatus::Finished)
			{
				bHasPendingTask = true;
				return EIterationResult::Break;
			}
			return EIterationResult::Continue;
		});

	return bHasPendingTask;
}

void FSceneState::AllocateTaskInstances(const FSceneStateExecutionContext& InContext, TConstArrayView<FConstStructView> InTemplateTaskInstances) const
{
	if (FInstancedStructContainer* const TaskInstanceContainer = InContext.FindOrAddTaskInstanceContainer(*this))
	{
		// Copy the Template data
		*TaskInstanceContainer = InTemplateTaskInstances;

		// Instance each Template Object into the Instance data
		InstanceTaskObjects(InContext.GetRootObject()
			, UE::SceneState::GetStructViews(*TaskInstanceContainer)
			, InTemplateTaskInstances
			, [](FObjectDuplicationParameters& InParams)->UObject*
			{
				return StaticDuplicateObjectEx(InParams);
			});
	}
}

void FSceneState::InstanceTaskObjects(UObject* InOuter, TConstArrayView<FStructView> InTargets, TConstArrayView<FConstStructView> InSources, TFunctionRef<UObject*(FObjectDuplicationParameters&)> InDuplicationFunc) const
{
	check(InTargets.Num() == InSources.Num());

	for (int32 TaskIndex = 0; TaskIndex < InSources.Num(); ++TaskIndex)
	{
		const FConstStructView Source = InSources[TaskIndex];
		const FStructView Target = InTargets[TaskIndex];

		check(Source.GetScriptStruct() == Target.GetScriptStruct());

		if (!Source.GetScriptStruct())
		{
			continue;
		}

		// Visit all object properties that are instanced references, stepping into structs but not object properties themselves.
		// Using const_cast here as there's no Visit variant taking a const ptr.
		Source.GetScriptStruct()->Visit(const_cast<uint8*>(Source.GetMemory()),
			[&InDuplicationFunc, InOuter, Target](const FPropertyVisitorContext& InContext)->EPropertyVisitorControlFlow
			{
				const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InContext.Path.Top().Property);
				if (!ObjectProperty)
				{
					return EPropertyVisitorControlFlow::StepInto;
				}

				if (!ObjectProperty->HasAnyPropertyFlags(CPF_InstancedReference))
				{
					return EPropertyVisitorControlFlow::StepOver;
				}

				const void* const SourcePropertyMemory = InContext.Data.PropertyData;

				UObject* InstanceObject = nullptr;
				if (UObject* TemplateObject = ObjectProperty->GetObjectPropertyValue(SourcePropertyMemory))
				{
					FObjectDuplicationParameters Parameters = InitStaticDuplicateObjectParams(TemplateObject, InOuter);
					Parameters.DestName = MakeUniqueObjectName(Parameters.DestOuter, Parameters.SourceObject->GetClass(), Parameters.SourceObject->GetFName());
					Parameters.FlagMask = RF_AllFlags & ~RF_DefaultSubObject;
					Parameters.PortFlags |= PPF_DuplicateVerbatim; // Skip resetting text IDs

					InstanceObject = InDuplicationFunc(Parameters);

					UE_LOG(LogSceneState, Verbose, TEXT("Instantiated object '%s' (Outer: '%s') from template '%s' (Outer: '%s')")
						, *GetNameSafe(InstanceObject)
						, *InOuter->GetName()
						, *TemplateObject->GetName()
						, *GetNameSafe(TemplateObject->GetOuter()));
				}

				void* const TargetPropertyMemory = UE::SceneState::ResolveVisitedPath(Target.GetScriptStruct(), Target.GetMemory(), InContext.Path);
				ObjectProperty->SetPropertyValue(TargetPropertyMemory, InstanceObject);
				return EPropertyVisitorControlFlow::StepOver;
			});
	}
}

void FSceneState::CaptureEvents(const FSceneStateExecutionContext& InContext) const
{
	if (USceneStateEventStream* EventStream = InContext.GetEventStream())
	{
		TConstArrayView<FSceneStateEventHandler> EventHandlers = InContext.GetEventHandlers(*this);
		EventStream->CaptureEvents(EventHandlers);
	}
}

void FSceneState::ResetCapturedEvents(const FSceneStateExecutionContext& InContext) const
{
	if (USceneStateEventStream* EventStream = InContext.GetEventStream())
	{
		TConstArrayView<FSceneStateEventHandler> EventHandlers = InContext.GetEventHandlers(*this);
		EventStream->ResetCapturedEvents(EventHandlers);
	}
}
