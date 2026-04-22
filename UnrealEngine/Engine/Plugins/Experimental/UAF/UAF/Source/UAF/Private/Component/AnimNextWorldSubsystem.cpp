// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/AnimNextWorldSubsystem.h"

#include "AnimNextDebugDraw.h"
#include "AnimNextRigVMAsset.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModuleInstance.h"
#include "Module/ModuleTaskContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextWorldSubsystem)

UAnimNextWorldSubsystem::UAnimNextWorldSubsystem()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
#if WITH_EDITOR
		OnCompileJobFinishedHandle = UAnimNextRigVMAsset::OnCompileJobFinished().AddUObject(this, &UAnimNextWorldSubsystem::OnCompileJobFinished);
#endif

		// Kick off root task at the start of each world tick
		OnWorldPreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([this](UWorld* InWorld, ELevelTick InTickType, float InDeltaSeconds)
		{
			if (InTickType == LEVELTICK_All || InTickType == LEVELTICK_ViewportsOnly)
			{
				// Flush actions here as they require game thread callbacks (e.g. to reconfigure tick functions)
				FlushPendingActions();
				DeltaTime = InDeltaSeconds;
			}
		});
	}
}

void UAnimNextWorldSubsystem::BeginDestroy()
{
	Super::BeginDestroy();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
#if WITH_EDITOR
		UAnimNextRigVMAsset::OnCompileJobFinished().Remove(OnCompileJobFinishedHandle);
#endif

		FWorldDelegates::OnWorldPreActorTick.Remove(OnWorldPreActorTickHandle);

		{
			FRWScopeLock InstancesLockScope(InstancesLock, SLT_Write);
			UE_MT_SCOPED_WRITE_ACCESS(InstancesAccessDetector);

			for (FAnimNextModuleInstance& Instance : Instances)
			{
				Instance.RemoveAllTickDependencies();
				Instance.ReleaseComponents();
			}
			Instances = UE::UAF::TPool<FAnimNextModuleInstance>(); // Force instances destruction
		}
	}
}

UAnimNextWorldSubsystem* UAnimNextWorldSubsystem::Get(UObject* InObject)
{
	if (InObject == nullptr)
	{
		return nullptr;
	}

	UWorld* World = InObject->GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	return World->GetSubsystem<UAnimNextWorldSubsystem>();
}

void UAnimNextWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UAnimNextWorldSubsystem* This = CastChecked<UAnimNextWorldSubsystem>(InThis);
	UE_MT_SCOPED_READ_ACCESS(This->InstancesAccessDetector);

	for (FAnimNextModuleInstance& Instance : This->Instances)
	{
		Collector.AddPropertyReferencesWithStructARO(FAnimNextModuleInstance::StaticStruct(), &Instance, InThis);
	}
}

bool UAnimNextWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	switch (WorldType)
	{
	case EWorldType::Game:
	case EWorldType::Editor:
	case EWorldType::PIE:
	case EWorldType::EditorPreview:
	case EWorldType::GamePreview:
		return true;
	}

	return false;
}

bool UAnimNextWorldSubsystem::IsValidHandle(UE::UAF::FModuleHandle InHandle) const
{
	UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
	return Instances.IsValidHandle(InHandle);
}

void UAnimNextWorldSubsystem::FlushPendingActions()
{
	using namespace UE::UAF;

	FRWScopeLock PendingLockScope(PendingLock, SLT_Write);
	UE_MT_SCOPED_WRITE_ACCESS(PendingActionsAccessDetector);

	if (PendingActions.Num() > 0)
	{
		for (FModulePendingAction& PendingAction : PendingActions)
		{
			switch (PendingAction.Type)
			{
			case FModulePendingAction::EType::ReleaseHandle:
				if (IsValidHandle(PendingAction.Handle))
				{
					FRWScopeLock InstancesLockScope(InstancesLock, SLT_Write);
					UE_MT_SCOPED_WRITE_ACCESS(InstancesAccessDetector);
					Instances.Release(PendingAction.Handle);
				}
				break;
			case FModulePendingAction::EType::EnableHandle:
				if (IsValidHandle(PendingAction.Handle))
				{
					UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
					FAnimNextModuleInstance& Instance = Instances.Get(PendingAction.Handle);
					Instance.Enable(PendingAction.Payload.Get<bool>());
				}
				break;
			case FModulePendingAction::EType::EnableDebugDrawing:
#if UE_ENABLE_DEBUG_DRAWING
				if (IsValidHandle(PendingAction.Handle))
				{
					UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
					FAnimNextModuleInstance& Instance = Instances.Get(PendingAction.Handle);
					Instance.ShowDebugDrawing(PendingAction.Payload.Get<bool>());
				}
#endif
				break;
			default:
				checkNoEntry();
				break;
			}
		}

		PendingActions.Reset();
	}
}

void UAnimNextWorldSubsystem::RegisterHandle(UE::UAF::FModuleHandle& InOutHandle, UAnimNextModule* InModule, UObject* InObject, EAnimNextModuleInitMethod InitMethod)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	{
		FWriteScopeLock InstancesLockScope(InstancesLock);
		UE_MT_SCOPED_WRITE_ACCESS(InstancesAccessDetector);

		InOutHandle = Instances.Emplace(InModule, InObject, &Instances, InitMethod);
	}

	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
		FAnimNextModuleInstance& Instance = Instances.Get(InOutHandle);
		Instance.Handle = InOutHandle;
		Instance.Initialize();
	}
}

void UAnimNextWorldSubsystem::UnregisterHandle(UE::UAF::FModuleHandle& InOutHandle)
{
	using namespace UE::UAF;
	check(IsInGameThread());

	if (IsValidHandle(InOutHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
		FAnimNextModuleInstance& Instance = Instances.Get(InOutHandle);

#if UE_ENABLE_DEBUG_DRAWING
		// Remove debug drawing immediately as the renderer will need to know about this before EOF
		Instance.DebugDraw->RemovePrimitive();
#endif

		// Remove all tick dependencies immediately, as once the handle has been invalidated there is no way for external systems to remove their dependencies
		Instance.RemoveAllTickDependencies();

		{
			FRWScopeLock PendingLockScope(PendingLock, SLT_Write);
			UE_MT_SCOPED_WRITE_ACCESS(PendingActionsAccessDetector);
			PendingActions.Emplace(InOutHandle, FModulePendingAction::EType::ReleaseHandle);
		}
		InOutHandle.Reset();
	}
}

bool UAnimNextWorldSubsystem::IsHandleEnabled(UE::UAF::FModuleHandle InHandle) const
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		// The last pending action takes precedent here if present
		{
			UE_MT_SCOPED_READ_ACCESS(PendingActionsAccessDetector);
			int32 PendingActionIndex = PendingActions.FindLastByPredicate([&InHandle](const FModulePendingAction& PendingAction) { return PendingAction.Handle == InHandle && PendingAction.Type == FModulePendingAction::EType::EnableHandle;});

			if (PendingActionIndex != INDEX_NONE)
			{
				return PendingActions[PendingActionIndex].Payload.Get<bool>();
			}
		}

		// Otherwise return the current value on the instance
		{
			UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
			const FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
			return Instance.IsEnabled();
		}
	}
	return false;
}

void UAnimNextWorldSubsystem::EnableHandle(UE::UAF::FModuleHandle InHandle, bool bInEnabled)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_WRITE_ACCESS(PendingActionsAccessDetector);
		PendingActions.Emplace(InHandle, FModulePendingAction::EType::EnableHandle, bInEnabled);
	}
}

#if UE_ENABLE_DEBUG_DRAWING
void UAnimNextWorldSubsystem::ShowDebugDrawingHandle(UE::UAF::FModuleHandle InHandle, bool bInShowDebugDrawing)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(PendingActionsAccessDetector);
		PendingActions.Emplace(InHandle, FModulePendingAction::EType::EnableDebugDrawing, bInShowDebugDrawing);
	}
}
#endif

void UAnimNextWorldSubsystem::QueueTaskHandle(UE::UAF::FModuleHandle InOutHandle, FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InOutHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);
		FAnimNextModuleInstance& Instance = Instances.Get(InOutHandle);
		Instance.QueueTask(InModuleEventName, MoveTemp(InTaskFunction), InLocation);
	}
}

#if WITH_EDITOR

void UAnimNextWorldSubsystem::OnCompileJobFinished(UAnimNextRigVMAsset* InAsset)
{
	// Cant do this while we are running in a world tick
	check(!GetWorld()->bInTick);
	UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

	for (FAnimNextModuleInstance& Instance : Instances)
	{
		if (Instance.GetModule() == InAsset)
		{
			Instance.OnCompileJobFinished();
		}
	}
}

#endif

void UAnimNextWorldSubsystem::QueueInputTraitEventHandle(UE::UAF::FModuleHandle InHandle, FAnimNextTraitEventPtr Event)
{
	using namespace UE::UAF;

	QueueTaskHandle(InHandle, NAME_None, [Event = MoveTemp(Event)](const FModuleTaskContext& InContext)
	{
		InContext.QueueInputTraitEvent(Event);
	},
	ETaskRunLocation::Before);
}

const FTickFunction* UAnimNextWorldSubsystem::FindTickFunctionHandle(UE::UAF::FModuleHandle InHandle, FName InEventName) const
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		const FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		const FModuleEventTickFunction* TickFunction = Instance.FindTickFunctionByName(InEventName);
		if (TickFunction == nullptr)
		{
			UE_LOGFMT(LogAnimation, Warning, "FindTickFunctionHandle: Could not find event '{EventName}' in module '{ModuleName}'", InEventName, Instance.GetAssetName());
			return nullptr;
		}

		if (TickFunction->bUserEvent)
		{
			return TickFunction;
		}

		UE_LOGFMT(LogAnimation, Warning, "FindTickFunctionHandle: Event '{EventName}' in module '{ModuleName}' is not a bUserEvent, therefore cannot be exposed", InEventName, Instance.GetAssetName());
	}
	return nullptr;
}

void UAnimNextWorldSubsystem::AddDependencyHandle(UE::UAF::FModuleHandle InHandle, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		FModuleEventTickFunction* TickFunction = Instance.FindTickFunctionByName(InEventName);
		if (TickFunction == nullptr)
		{
			UE_LOGFMT(LogAnimation, Warning, "AddDependencyHandle: Could not find event '{EventName}' in module '{ModuleName}'", InEventName, Instance.GetAssetName());
			return;
		}

		if (InDependency == EDependency::Prerequisite)
		{
			TickFunction->AddPrerequisite(InObject, InTickFunction);
		}
		else
		{
			TickFunction->AddSubsequent(InObject, InTickFunction);
		}
	}
}

void UAnimNextWorldSubsystem::RemoveDependencyHandle(UE::UAF::FModuleHandle InHandle, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		FModuleEventTickFunction* TickFunction = Instance.FindTickFunctionByName(InEventName);
		if (TickFunction == nullptr)
		{
			UE_LOGFMT(LogAnimation, Warning, "RemoveDependencyHandle: Could not find event '{EventName}' in module '{ModuleName}'", InEventName, Instance.GetAssetName());
			return;
		}

		if (InDependency == EDependency::Prerequisite)
		{
			TickFunction->RemovePrerequisite(InObject, InTickFunction);
		}
		else
		{
			TickFunction->RemoveSubsequent(InObject, InTickFunction);
		}
	}
}

void UAnimNextWorldSubsystem::AddModuleEventDependencyHandle(UE::UAF::FModuleHandle InHandle, FName InEventName, UE::UAF::FModuleHandle OtherHandle, FName OtherEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InHandle != OtherHandle);
	if (IsValidHandle(OtherHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(OtherHandle);
		FModuleEventTickFunction* TickFunction = Instance.FindTickFunctionByName(OtherEventName);
		if (TickFunction == nullptr)
		{
			UE_LOGFMT(LogAnimation, Warning, "AddDependencyHandle: Could not find event '{EventName}' in module '{ModuleName}'", OtherEventName, Instance.GetAssetName());
			return;
		}

		AddDependencyHandle(InHandle, Instance.GetObject(), *TickFunction, InEventName, InDependency);
	}
}

void UAnimNextWorldSubsystem::RemoveModuleEventDependencyHandle(UE::UAF::FModuleHandle InHandle, FName InEventName, UE::UAF::FModuleHandle OtherHandle, FName OtherEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InHandle != OtherHandle);
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		FModuleEventTickFunction* TickFunction = Instance.FindTickFunctionByName(InEventName);
		if (TickFunction == nullptr)
		{
			UE_LOGFMT(LogAnimation, Warning, "RemoveDependencyHandle: Could not find event '{EventName}' in module '{ModuleName}'", OtherEventName, Instance.GetAssetName());
			return;
		}

		RemoveDependencyHandle(InHandle, Instance.GetObject(), *TickFunction, InEventName, InDependency);
	}
}

EPropertyBagResult UAnimNextWorldSubsystem::SetVariableHandle(UE::UAF::FModuleHandle InHandle, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData)
{
	using namespace UE::UAF;
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		return Instance.SetProxyVariable(InVariable, InType, InData);
	}
	return EPropertyBagResult::PropertyNotFound;
}

EPropertyBagResult UAnimNextWorldSubsystem::WriteVariableHandle(UE::UAF::FModuleHandle InHandle, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction)
{
	using namespace UE::UAF;
	if (IsValidHandle(InHandle))
	{
		UE_MT_SCOPED_READ_ACCESS(InstancesAccessDetector);

		FAnimNextModuleInstance& Instance = Instances.Get(InHandle);
		return Instance.WriteProxyVariable(InVariable, InType, InFunction);
	}
	return EPropertyBagResult::PropertyNotFound;
}
