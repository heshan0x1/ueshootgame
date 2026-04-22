// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModuleInstance.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "AnimNextDebugDraw.h"
#endif
#include "AnimNextPool.h"
#include "Engine/World.h"
#include "AnimNextStats.h"
#include "Async/TaskGraphInterfaces.h"
#include "SceneInterface.h"
#include "UAFRigVMComponent.h"
#include "Algo/TopologicalSort.h"
#include "Logging/StructuredLog.h"
#include "Misc/EnumerateRange.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Module/ModuleTickFunction.h"
#include "UObject/UObjectIterator.h"
#include "RewindDebugger/AnimNextTrace.h"
#include "Variables/AnimNextVariableReference.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextModuleInstance)

DEFINE_STAT(STAT_AnimNext_InitializeInstance);

FAnimNextModuleInstance::FAnimNextModuleInstance() = default;

FAnimNextModuleInstance::FAnimNextModuleInstance(
		UAnimNextModule* InModule,
		UObject* InObject,
		UE::UAF::TPool<FAnimNextModuleInstance>* InPool,
		EAnimNextModuleInitMethod InInitMethod)
	: Object(InObject)
	, Pool(InPool)
	, InitState(EInitState::NotInitialized)
	, RunState(ERunState::NotInitialized)
	, InitMethod(InInitMethod)
{
	check(InModule);
	check(InObject);

	Asset = InModule;

#if UE_ENABLE_DEBUG_DRAWING
	if(Object && Object->GetWorld())
	{
		DebugDraw = MakeUnique<UE::UAF::Debug::FDebugDraw>(Object);
	}
#endif
}

FAnimNextModuleInstance::~FAnimNextModuleInstance()
{
	ResetBindingsAndInstanceData();

#if UE_ENABLE_DEBUG_DRAWING
	DebugDraw.Reset();
#endif

	Object = nullptr;
	Asset = nullptr;
	Handle.Reset();
}

namespace UE::UAF::Private
{

struct FImplementedModuleEvent
{
	UScriptStruct* Struct = nullptr;
	FModuleEventBindingFunction Binding;
	FName EventName;
	EModuleEventPhase Phase = EModuleEventPhase::Execute;
	ETickingGroup TickGroup = ETickingGroup::TG_PrePhysics;
	int32 SortOrder = 0;
	bool bUserEvent = false;
	bool bIsTask = false;
	bool bIsGameThreadTask = false;
};

static TArray<FImplementedModuleEvent> GImplementedModuleEvents;

// Gets information about the module events that are implemented by the supplied VM, sorted by execution order in the frame
static TConstArrayView<FImplementedModuleEvent> GetImplementedModuleEvents(const URigVM* VM)
{
	check(IsInGameThread());	// This function cannot be run concurrently because of static usage
	GImplementedModuleEvents.Reset();

	const FRigVMByteCode& ByteCode = VM->GetByteCode();
	const TArray<const FRigVMFunction*>& Functions = VM->GetFunctions();
	FRigVMInstructionArray Instructions = ByteCode.GetInstructions();
	for (int32 EntryIndex = 0; EntryIndex < ByteCode.NumEntries(); EntryIndex++)
	{
		const FRigVMByteCodeEntry& Entry = ByteCode.GetEntry(EntryIndex);
		const FRigVMInstruction& Instruction = Instructions[Entry.InstructionIndex];
		const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instruction);
		const FRigVMFunction* Function = Functions[Op.FunctionIndex];
		check(Function != nullptr);

		if (Function->Struct->IsChildOf(FRigUnit_AnimNextModuleEventBase::StaticStruct()))
		{
			TInstancedStruct<FRigUnit_AnimNextModuleEventBase> StructInstance;
			StructInstance.InitializeAsScriptStruct(Function->Struct);
			const FRigUnit_AnimNextModuleEventBase& Event = StructInstance.Get();
			FImplementedModuleEvent& NewEvent = GImplementedModuleEvents.AddDefaulted_GetRef();
			NewEvent.Struct = Function->Struct;
			NewEvent.Binding = Event.GetBindingFunction();
			NewEvent.EventName = Event.GetEventName();
			NewEvent.Phase = Event.GetEventPhase();
			NewEvent.TickGroup = Event.GetTickGroup();
			NewEvent.SortOrder = Event.GetSortOrder();
			NewEvent.bUserEvent = Event.IsUserEvent();
			NewEvent.bIsTask = Event.IsTask();
			NewEvent.bIsGameThreadTask = Event.IsGameThreadTask();

			// User events can override their event name etc. via parameters 
			if (Function->Struct->IsChildOf(FRigUnit_AnimNextUserEvent::StaticStruct()))
			{
				// Pull the values out of the literal memory
				FRigVMOperandArray Operands = ByteCode.GetOperandsForExecuteOp(Instruction);
				check(Function->ArgumentNames.Num() == Operands.Num());
				int32 NumOperands = Operands.Num();
				URigVM* NonConstVM = const_cast<URigVM*>(VM);
				for (int32 OperandIndex = 0; OperandIndex < NumOperands; ++OperandIndex)
				{
					const FRigVMOperand& Operand = Operands[OperandIndex];
					FName OperandName = Function->ArgumentNames[OperandIndex];
					if (OperandName == GET_MEMBER_NAME_CHECKED(FRigUnit_AnimNextUserEvent, Name))
					{
						check(Operand.GetMemoryType() == ERigVMMemoryType::Literal);
						NewEvent.EventName = *NonConstVM->LiteralMemoryStorage.GetData<FName>(Operand.GetRegisterIndex());
					}
					else if (OperandName == GET_MEMBER_NAME_CHECKED(FRigUnit_AnimNextUserEvent, SortOrder))
					{
						check(Operand.GetMemoryType() == ERigVMMemoryType::Literal);
						NewEvent.SortOrder = *NonConstVM->LiteralMemoryStorage.GetData<int32>(Operand.GetRegisterIndex());
					}
				}
			}
		}
	}

	Algo::Sort(GImplementedModuleEvents, [](const FImplementedModuleEvent& InA, const FImplementedModuleEvent& InB)
	{
		if (InA.Phase != InB.Phase)
		{
			return InA.Phase < InB.Phase;
		}
		else if (InA.TickGroup != InB.TickGroup)
		{
			return InA.TickGroup < InB.TickGroup;
		}
		else if (InA.SortOrder != InB.SortOrder)
		{
			return InA.SortOrder < InB.SortOrder;
		}

		// Tie-break sorting on event name for determinism
		return InA.EventName.Compare(InB.EventName) < 0;
	});
	
	return GImplementedModuleEvents;
}

}

void FAnimNextModuleInstance::Initialize()
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_InitializeInstance);
	
	using namespace UE::UAF;

	check(IsInGameThread());

	check(Object);
	if (!Object)
	{
		return;
	}
	
	const UAnimNextModule* Module = GetModule();

	check(Module);
	if (Module)
	{
#if WITH_EDITOR
		if (Module->CompilationState == EAnimNextRigVMAssetState::CompiledWithErrors)
		{
			return;
		}
#endif
	}
	else
	{
		return;
	}

	UWorld* World = Object->GetWorld();
	if(World)
	{
		WorldType = World->WorldType;
	}

	// Get all the module events from the VM entry points, sorted by ordering in the frame
	URigVM* VM = Module->RigVM;
	TConstArrayView<Private::FImplementedModuleEvent> ImplementedModuleEvents = Private::GetImplementedModuleEvents(VM);

	// Setup tick function graph using module events
	if (ImplementedModuleEvents.Num() > 0)
	{
		TransitionToInitState(EInitState::CreatingTasks);

		// Allocate tick functions
		TickFunctions.Reserve(ImplementedModuleEvents.Num());
		bool bFoundFirstUserEvent = false;
		FModuleEventTickFunction* PrevTickFunction = nullptr;
		for (int32 EventIndex = 0; EventIndex < ImplementedModuleEvents.Num(); EventIndex++)
		{
			const Private::FImplementedModuleEvent& ModuleEvent = ImplementedModuleEvents[EventIndex];
			if (!ModuleEvent.bIsTask)
			{
				continue;
			}

			FModuleEventTickFunction& TickFunction = TickFunctions.AddDefaulted_GetRef();
			TickFunction.bRunOnAnyThread = !ModuleEvent.bIsGameThreadTask;
			TickFunction.ModuleInstance = this;
			TickFunction.EventName = ModuleEvent.EventName;
			TickFunction.TickGroup = ModuleEvent.TickGroup;
			TickFunction.bUserEvent = ModuleEvent.bUserEvent;

			// Perform custom setup
			FTickFunctionBindingContext Context(*this, Object, World);
			ModuleEvent.Binding(Context, TickFunction);

			// Establish linear dependency chain
			if (PrevTickFunction != nullptr)
			{
				TickFunction.AddPrerequisite(Object, *PrevTickFunction);
			}
			PrevTickFunction = &TickFunction;

			// Set up dependencies, if any
			for (const TInstancedStruct<FRigVMTrait_ModuleEventDependency>& DependencyInstance : Module->Dependencies)
			{
				const FRigVMTrait_ModuleEventDependency* Dependency = DependencyInstance.GetPtr<FRigVMTrait_ModuleEventDependency>();
				if (Dependency != nullptr && Dependency->EventName == ModuleEvent.EventName)
				{
					FModuleDependencyContext ModuleDependencyContext(Object, TickFunction);
					Dependency->OnAddDependency(ModuleDependencyContext);
				}
			}

			if (ModuleEvent.bUserEvent && !ModuleEvent.bIsGameThreadTask)
			{
				// Hook up with Task Sync Manager if possible
				TickFunction.InitializeBatchedWork(World);
			}

			if (ModuleEvent.bUserEvent && !bFoundFirstUserEvent)
			{
				TickFunction.bFirstUserEvent = true;
				bFoundFirstUserEvent = true;

				// Set this first user event to run the bindings event, if it exists
				auto IsExecuteBindingsEvent = [](const Private::FImplementedModuleEvent& InEvent)
				{
					return InEvent.EventName == FRigUnit_AnimNextExecuteBindings_WT::EventName;
				};

				TickFunction.bRunBindingsEvent = ImplementedModuleEvents.ContainsByPredicate(IsExecuteBindingsEvent);
			}
		}

		// Find the last user event - 'end' logic will be called from here
		for (int32 EventIndex = TickFunctions.Num() - 1; EventIndex >= 0; EventIndex--)
		{
			FModuleEventTickFunction& TickFunction = TickFunctions[EventIndex];
			if (TickFunction.bUserEvent)
			{
				TickFunction.bLastUserEvent = true;
				break;
			}
		}

		TransitionToInitState(EInitState::BindingTasks);

		// Register our tick functions
		if(World)
		{
			ULevel* Level = World->PersistentLevel;
			for (FModuleEventTickFunction& TickFunction : TickFunctions)
			{
				TickFunction.RegisterTickFunction(Level);
			}
		}

		TransitionToInitState(EInitState::SetupVariables);

		// TODO: code in EInitState::SetupVariables phase below can probably move to FModuleEventTickFunction::Initialize

		// Initialize variables
#if WITH_EDITOR
		if(bIsRecreatingOnCompile && Variables.bHasBeenInitialized)
		{
			MigrateVariables();
		}
		else
#endif
		{
			InitializeVariables();
		}

		ProxyVariables[0].Initialize(Variables);
		ProxyVariables[1].Initialize(Variables);

		// Allocate compiled-in module components
		{
			FUAFAssetInstanceComponent::FScopedConstructorHelper ConstructorHelper(*this);
			for(const UScriptStruct* ComponentStruct : Module->RequiredComponents)
			{
				TInstancedStruct<FUAFModuleInstanceComponent> InstancedStruct(ComponentStruct);
				ComponentMap.Add(ComponentStruct, MoveTemp(InstancedStruct));
			}
		}

		TransitionToInitState(EInitState::PendingInitializeEvent);
		TransitionToRunState(ERunState::Running);

		// Just pause now if we arent needing an initial update
		if(InitMethod == EAnimNextModuleInitMethod::None)
		{
			Enable(false);
		}
#if WITH_EDITOR
		else if(World)
		{
			// In editor worlds we run a linearized 'initial tick' to ensure we generate an initial output pose, as these worlds dont always tick
			if( World->WorldType == EWorldType::Editor ||
				World->WorldType == EWorldType::EditorPreview)
			{
				FModuleEventTickFunction::InitializeAndRunModule(*this);
			}
		}
#endif
	}
}

void FAnimNextModuleInstance::RemoveAllTickDependencies()
{
	using namespace UE::UAF;

	check(IsInGameThread());

	for (FModuleEventTickFunction& TickFunction : TickFunctions)
	{
		TickFunction.RemoveAllExternalSubsequents();
	}
}

void FAnimNextModuleInstance::ResetBindingsAndInstanceData()
{
	using namespace UE::UAF;

	check(IsInGameThread());

	TransitionToInitState(EInitState::NotInitialized);
	TransitionToRunState(ERunState::NotInitialized);

#if WITH_EDITOR
	if(!bIsRecreatingOnCompile)
#endif
	{
		for (FModuleEventTickFunction& TickFunction : TickFunctions)
		{
			// We should have released all external dependencies by now via RemoveAllTickDependencies
			check(TickFunction.ExternalSubsequents.Num() == 0);
			TickFunction.UnRegisterTickFunction();
		}
		ProxyVariables[0].Reset();
		ProxyVariables[1].Reset();
	}

	TickFunctions.Reset();

	ReleaseComponents();
}

void FAnimNextModuleInstance::QueueInputTraitEvent(FAnimNextTraitEventPtr Event)
{
	InputEventList.Push(MoveTemp(Event));
}

void FAnimNextModuleInstance::QueueOutputTraitEvent(FAnimNextTraitEventPtr Event)
{
	OutputEventList.Push(MoveTemp(Event));
}

bool FAnimNextModuleInstance::IsEnabled() const
{
	using namespace UE::UAF;

	check(IsInGameThread());

	return RunState == ERunState::Running;
}

void FAnimNextModuleInstance::Enable(bool bInEnabled)
{
	using namespace UE::UAF;

	check(IsInGameThread());

	if(RunState == ERunState::Paused || RunState == ERunState::Running)
	{
		for (FModuleEventTickFunction& TickFunction : TickFunctions)
		{
			TickFunction.SetTickFunctionEnable(bInEnabled);
		}

		TransitionToRunState(bInEnabled ? ERunState::Running : ERunState::Paused);
	}
}

void FAnimNextModuleInstance::TransitionToInitState(EInitState InNewState)
{
	switch(InNewState)
	{
	case EInitState::NotInitialized:
		check(InitState == EInitState::NotInitialized || InitState == EInitState::PendingInitializeEvent || InitState == EInitState::SetupVariables || InitState == EInitState::FirstUpdate || InitState == EInitState::Initialized);
		break;
	case EInitState::CreatingTasks:
		check(InitState == EInitState::NotInitialized);
		break;
	case EInitState::BindingTasks:
		check(InitState == EInitState::CreatingTasks);
		break;
	case EInitState::SetupVariables:
		check(InitState == EInitState::BindingTasks);
		break;
	case EInitState::PendingInitializeEvent:
		check(InitState == EInitState::SetupVariables);
		break;
	case EInitState::FirstUpdate:
		check(InitState == EInitState::PendingInitializeEvent);
		break;
	case EInitState::Initialized:
		check(InitState == EInitState::FirstUpdate);
		break;
	default:
		checkNoEntry();
	}

	InitState = InNewState;
}

void FAnimNextModuleInstance::TransitionToRunState(ERunState InNewState)
{
	switch(InNewState)
	{
	case ERunState::Running:
		check(RunState == ERunState::NotInitialized || RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	case ERunState::Paused:
		check(RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	case ERunState::NotInitialized:
		check(RunState == ERunState::NotInitialized || RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	default:
		checkNoEntry();
	}

	RunState = InNewState;
}

void FAnimNextModuleInstance::CopyProxyVariables()
{
	int32 ProxyReadIndex = 0;
	{
		FWriteScopeLock WriteLock(ProxyLock);

		// Flip the write buffer index
		ProxyReadIndex = ProxyWriteIndex;
		ProxyWriteIndex = 1 - ProxyWriteIndex;
	}

	FUAFInstanceVariableDataProxy& ProxyVariablesRead = ProxyVariables[ProxyReadIndex];
	ProxyVariablesRead.CopyDirty();
}

#if ANIMNEXT_TRACE_ENABLED
void FAnimNextModuleInstance::Trace()
{
	if (!bTracedThisFrame)
	{
		TRACE_ANIMNEXT_VARIABLES(this, Object);
		bTracedThisFrame = true;
	}
}
#endif

const UAnimNextModule* FAnimNextModuleInstance::GetModule() const
{
	return CastChecked<UAnimNextModule>(Asset, ECastCheckedType::NullAllowed);
}

#if WITH_EDITOR
void FAnimNextModuleInstance::OnCompileJobFinished()
{
	using namespace UE::UAF;

	FGuardValue_Bitfield(bIsRecreatingOnCompile, true);

	ResetBindingsAndInstanceData();
	Initialize();
}
#endif

#if UE_ENABLE_DEBUG_DRAWING
FRigVMDrawInterface* FAnimNextModuleInstance::GetDebugDrawInterface()
{
	return DebugDraw ? &DebugDraw->DrawInterface : nullptr;
}

void FAnimNextModuleInstance::ShowDebugDrawing(bool bInShowDebugDrawing)
{
	if(DebugDraw)
	{
		DebugDraw->SetEnabled(bInShowDebugDrawing);
	}
}
#endif

void FAnimNextModuleInstance::RunTaskOnGameThread(TUniqueFunction<void(void)>&& InFunction)
{
	UE::UAF::FModuleEventTickFunction::RunTaskOnGameThread(MoveTemp(InFunction));
}

UE::UAF::FModuleEventTickFunction* FAnimNextModuleInstance::FindTickFunctionByName(FName InEventName)
{
	using namespace UE::UAF;

	return const_cast<FModuleEventTickFunction*>(const_cast<const FAnimNextModuleInstance*>(this)->FindTickFunctionByName(InEventName));
}

const UE::UAF::FModuleEventTickFunction* FAnimNextModuleInstance::FindTickFunctionByName(FName InEventName) const
{
	using namespace UE::UAF;

	for(const FModuleEventTickFunction& TickFunction : TickFunctions)
	{
		if (TickFunction.EventName == InEventName)
		{
			return &TickFunction;
		}
	}
	return nullptr;
}

void FAnimNextModuleInstance::EndExecution(float InDeltaTime)
{
	// Give the module a chance to handle events
	RaiseTraitEvents(OutputEventList);

	// Give each component a chance to finalize execution
	for(auto It = ComponentMap.CreateIterator(); It; ++It)
	{
		FUAFModuleInstanceComponent* Component = It->Value.GetMutablePtr<FUAFModuleInstanceComponent>();
		if (Component == nullptr)
		{
			continue;
		}
		Component->OnEndExecution(InDeltaTime);
	}
}

void FAnimNextModuleInstance::RaiseTraitEvents(const UE::UAF::FTraitEventList& EventList)
{
	for(auto It = ComponentMap.CreateIterator(); It; ++It)
	{
		FUAFModuleInstanceComponent* Component = It->Value.GetMutablePtr<FUAFModuleInstanceComponent>();
		if (Component == nullptr)
		{
			continue;
		}

		// Event handlers can raise events and as such the list may change while we iterate
		// However, if an event is added while we iterate, we will not visit it
		const int32 NumEvents = EventList.Num();
		for (int32 EventIndex = 0; EventIndex < NumEvents; ++EventIndex)
		{
			const FAnimNextTraitEventPtr Event = EventList[EventIndex];
			if (Event->IsValid())
			{
				Component->OnTraitEvent(*Event);
			}
		}
	}
}

EPropertyBagResult FAnimNextModuleInstance::SetProxyVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData)
{
	FWriteScopeLock WriteLock(ProxyLock);
	FUAFInstanceVariableDataProxy& Proxy = ProxyVariables[ProxyWriteIndex];
	return Proxy.SetVariable(InVariable, InType, InData);
}

EPropertyBagResult FAnimNextModuleInstance::WriteProxyVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction)
{
	FWriteScopeLock WriteLock(ProxyLock);
	FUAFInstanceVariableDataProxy& Proxy = ProxyVariables[ProxyWriteIndex];
	TArrayView<uint8> Data;
	EPropertyBagResult Result = Proxy.WriteVariable(InVariable, InType, Data);
	if (Result == EPropertyBagResult::Success)
	{
		InFunction(Data);
	}
	return Result;
}

void FAnimNextModuleInstance::RunRigVMEvent(FName InEventName, float InDeltaTime)
{
	URigVM* VM = GetModule()->RigVM;
	if(VM == nullptr)
	{
		return;
	}

	if(!VM->ContainsEntry(InEventName))
	{
		return;
	}

	FUAFRigVMComponent& RigVMComponent = GetComponent<FUAFRigVMComponent>();
	FRigVMExtendedExecuteContext& ExtendedExecuteContext = RigVMComponent.GetExtendedExecuteContext();
	check(ExtendedExecuteContext.VMHash == VM->GetVMHash());

	FAnimNextExecuteContext& AnimNextContext = ExtendedExecuteContext.GetPublicDataSafe<FAnimNextExecuteContext>();

	// RigVM setup
	AnimNextContext.SetDeltaTime(InDeltaTime);
	AnimNextContext.SetOwningObject(Object);

#if UE_ENABLE_DEBUG_DRAWING
	AnimNextContext.SetDrawInterface(GetDebugDrawInterface());
#endif

	// Insert our context data for the scope of execution
	FAnimNextModuleContextData ContextData(this);
	UE::UAF::FScopedExecuteContextData ContextDataScope(AnimNextContext, ContextData);

	// Run the VM for this event
	VM->ExecuteVM(ExtendedExecuteContext, InEventName);
}


TArrayView<UE::UAF::FModuleEventTickFunction> FAnimNextModuleInstance::GetTickFunctions()
{
	return TickFunctions;
}

UE::UAF::FModuleEventTickFunction* FAnimNextModuleInstance::FindFirstUserTickFunction()
{
	UE::UAF::FModuleEventTickFunction* FoundTickFunction = TickFunctions.FindByPredicate([](const UE::UAF::FModuleEventTickFunction& InTickFunction)
	{
		return InTickFunction.bFirstUserEvent;
	});

	return FoundTickFunction;
}

void FAnimNextModuleInstance::QueueTask(FName InEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation)
{
	using namespace UE::UAF;

	FModuleEventTickFunction* FoundTickFunction = nullptr;
	if(TickFunctions.Num() > 0)
	{
		if(!InEventName.IsNone())
		{
			// Match according to event desc
			FoundTickFunction = TickFunctions.FindByPredicate([InEventName](const FModuleEventTickFunction& InTickFunction)
			{
				return InTickFunction.EventName == InEventName;
			});
		}

		if (FoundTickFunction == nullptr)
		{
			// Fall back to first user function
			FoundTickFunction = TickFunctions.FindByPredicate([](const UE::UAF::FModuleEventTickFunction& InTickFunction)
			{
				return InTickFunction.bFirstUserEvent;
			});
		}
	}

	TSpscQueue<TUniqueFunction<void(const FModuleTaskContext&)>>* Queue = nullptr;
	if (FoundTickFunction)
	{
		switch (InLocation)
		{
		case ETaskRunLocation::Before:
			Queue = &FoundTickFunction->PreExecuteTasks;
			break;
		case ETaskRunLocation::After:
			Queue = &FoundTickFunction->PostExecuteTasks;
			break;
		}
	}

	if (Queue)
	{
		Queue->Enqueue(MoveTemp(InTaskFunction));
	}
	else
	{
		UE_LOGFMT(LogAnimation, Warning, "QueueTask: Could not find event '{EventName}' in module '{ModuleName}'", InEventName, GetAssetName());
	}
}

void FAnimNextModuleInstance::QueueTaskOnOtherModule(const UE::UAF::FModuleHandle InOtherModuleHandle, const FName InEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation)
{
	if (Pool == nullptr)
	{
		return;
	}

	FAnimNextModuleInstance* OtherModuleInstance = Pool->TryGet(InOtherModuleHandle);
	if (OtherModuleInstance == nullptr)
	{
		return;
	}

	OtherModuleInstance->QueueTask(InEventName, MoveTemp(InTaskFunction), InLocation);
}
