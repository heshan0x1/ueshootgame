// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextComponentWorldSubsystem.h"

#include "Component/AnimNextComponent.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModule.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTaskContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextComponentWorldSubsystem)

void UAnimNextComponentWorldSubsystem::Register(UAnimNextComponent* InComponent)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	RegisterHandle(InComponent->ModuleHandle, InComponent->Module, InComponent, InComponent->InitMethod);
}

void UAnimNextComponentWorldSubsystem::Unregister(UAnimNextComponent* InComponent)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	UnregisterHandle(InComponent->ModuleHandle);
}

void UAnimNextComponentWorldSubsystem::SetEnabled(UAnimNextComponent* InComponent, bool bInEnabled)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	EnableHandle(InComponent->ModuleHandle, bInEnabled);
}

bool UAnimNextComponentWorldSubsystem::IsEnabled(const UAnimNextComponent* InComponent) const
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	return IsHandleEnabled(InComponent->ModuleHandle);
}

#if UE_ENABLE_DEBUG_DRAWING
void UAnimNextComponentWorldSubsystem::ShowDebugDrawing(UAnimNextComponent* InComponent, bool bInShowDebugDrawing)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	ShowDebugDrawingHandle(InComponent->ModuleHandle, bInShowDebugDrawing);
}
#endif

void UAnimNextComponentWorldSubsystem::QueueTask(UAnimNextComponent* InComponent, FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	QueueTaskHandle(InComponent->ModuleHandle, InModuleEventName, MoveTemp(InTaskFunction), InLocation);
}

void UAnimNextComponentWorldSubsystem::QueueInputTraitEvent(UAnimNextComponent* InComponent, FAnimNextTraitEventPtr Event)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	QueueInputTraitEventHandle(InComponent->ModuleHandle, Event);
}

const FTickFunction* UAnimNextComponentWorldSubsystem::FindTickFunction(const UAnimNextComponent* InComponent, FName InEventName) const
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	return FindTickFunctionHandle(InComponent->ModuleHandle, InEventName);
}

void UAnimNextComponentWorldSubsystem::AddDependency(UAnimNextComponent* InComponent, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	AddDependencyHandle(InComponent->ModuleHandle, InObject, InTickFunction, InEventName, InDependency);
}

void UAnimNextComponentWorldSubsystem::RemoveDependency(UAnimNextComponent* InComponent, UObject* InObject, FTickFunction& InTickFunction, FName InEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent);
	RemoveDependencyHandle(InComponent->ModuleHandle, InObject, InTickFunction, InEventName, InDependency);
}

void UAnimNextComponentWorldSubsystem::AddModuleEventDependency(UAnimNextComponent* InComponent, FName InEventName, UAnimNextComponent* OtherComponent, FName OtherEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent && OtherComponent && InComponent != OtherComponent);
	AddModuleEventDependencyHandle(InComponent->ModuleHandle, InEventName, OtherComponent->ModuleHandle, OtherEventName, InDependency);
}

void UAnimNextComponentWorldSubsystem::RemoveModuleEventDependency(UAnimNextComponent* InComponent, FName InEventName, UAnimNextComponent* OtherComponent, FName OtherEventName, EDependency InDependency)
{
	using namespace UE::UAF;
	check(IsInGameThread());
	check(InComponent && OtherComponent && InComponent != OtherComponent);
	RemoveModuleEventDependencyHandle(InComponent->ModuleHandle, InEventName, OtherComponent->ModuleHandle, OtherEventName, InDependency);
}

EPropertyBagResult UAnimNextComponentWorldSubsystem::SetVariable(UAnimNextComponent* InComponent, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InData)
{
	using namespace UE::UAF;
	check(InComponent);
	return SetVariableHandle(InComponent->ModuleHandle, InVariable, InType, InData);
}

EPropertyBagResult UAnimNextComponentWorldSubsystem::WriteVariable(UAnimNextComponent* InComponent, const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction)
{
	using namespace UE::UAF;
	check(InComponent);
	return WriteVariableHandle(InComponent->ModuleHandle, InVariable, InType, InFunction);
}
