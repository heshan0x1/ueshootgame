// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/AnimNextComponent.h"

#include "AnimNextComponentWorldSubsystem.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Engine/World.h"
#include "Module/ModuleTaskContext.h"
#include "Variables/AnimNextVariableReference.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextComponent)

void UAnimNextComponent::OnRegister()
{
	using namespace UE::UAF;

	Super::OnRegister();

	Subsystem = GetWorld()->GetSubsystem<UAnimNextComponentWorldSubsystem>();

	if (Subsystem && Module)
	{
		check(!ModuleHandle.IsValid());

		Subsystem->Register(this);
#if UE_ENABLE_DEBUG_DRAWING
		Subsystem->ShowDebugDrawing(this, bShowDebugDrawing);
#endif
	}
}

void UAnimNextComponent::OnUnregister()
{
	Super::OnUnregister();

	if(Subsystem)
	{
		Subsystem->Unregister(this);
		Subsystem = nullptr;
	}
}

void UAnimNextComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	check(World);

	if (InitMethod == EAnimNextModuleInitMethod::InitializeAndRun
	|| (InitMethod == EAnimNextModuleInitMethod::InitializeAndPauseInEditor && World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
	{
		SetEnabled(true);
	}
}

void UAnimNextComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	SetEnabled(false);
}

void UAnimNextComponent::BlueprintSetVariable(FName Name, int32 Value)
{
	checkNoEntry();
}

DEFINE_FUNCTION(UAnimNextComponent::execBlueprintSetVariable)
{
	using namespace UE::UAF;

	// Read wildcard Value input.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;

	P_GET_PROPERTY(FNameProperty, Name);

	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ValueProp = CastField<FProperty>(Stack.MostRecentProperty);
	const void* ContainerPtr = Stack.MostRecentPropertyContainer;

	P_FINISH;

	if (!ValueProp || !ContainerPtr)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableError", "Failed to resolve the Value for Set Variable")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	if (Name == NAME_None)
	{
		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableInvalidNameWarning", "Invalid variable name supplied to Set Variable")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	EPropertyBagResult Result = EPropertyBagResult::Success;

	P_NATIVE_BEGIN;

	{
		FAnimNextParamType Type = FAnimNextParamType::FromProperty(ValueProp);
		const uint8* ValuePtr = ValueProp->ContainerPtrToValuePtr<uint8>(ContainerPtr);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Result = P_THIS->SetVariableInternal(FAnimNextVariableReference(Name), Type, TConstArrayView<uint8>(ValuePtr, ValueProp->GetElementSize()));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	P_NATIVE_END;

	switch (Result)
	{
	case EPropertyBagResult::Success:
		break;
	case EPropertyBagResult::TypeMismatch:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableTypeMismatch", "Incompatible type supplied for variable '{0}'"), FText::FromName(Name))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	case EPropertyBagResult::PropertyNotFound:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableNotFoundWarning", "Unknown variable name '{0}' supplied to Set Variable"), FText::FromName(Name))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	case EPropertyBagResult::OutOfBounds:
	case EPropertyBagResult::DuplicatedValue:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableUnknownWarning", "Unexpected internal error when calling Set Variable for '{0}'"), FText::FromName(Name))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	}
}

void UAnimNextComponent::BlueprintSetVariableReference(const FAnimNextVariableReference& Variable, const int32& Value)
{
	checkNoEntry();
}

DEFINE_FUNCTION(UAnimNextComponent::execBlueprintSetVariableReference)
{
	using namespace UE::UAF;

	P_GET_STRUCT_REF(FAnimNextVariableReference, Variable);

	const FProperty* VariableProperty = Variable.ResolveProperty();
	if (VariableProperty == nullptr)
	{
		P_FINISH;

		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AbortExecution,
			FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariablePropertyError", "Failed to resolve the property {0} of variable reference"), FText::FromName(Variable.GetName()))
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	if (Variable.GetName() == NAME_None || Variable.GetObject() == nullptr)
	{
		P_FINISH;

		FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::NonFatalError,
			NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableInvalidWarning", "Invalid variable supplied to Set Variable")
		);

		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		return;
	}

	// Read wildcard Value input.
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;

	EPropertyBagResult Result = EPropertyBagResult::Success;
	{
		uint8* TempStorage = (uint8*)FMemory_Alloca_Aligned(VariableProperty->GetElementSize(), VariableProperty->GetMinAlignment());
		VariableProperty->InitializeValue(TempStorage);

		Stack.StepCompiledIn(TempStorage, VariableProperty->GetClass());

		P_FINISH;
		
		P_NATIVE_BEGIN;

		{
			FAnimNextParamType Type = FAnimNextParamType::FromProperty(VariableProperty);
			Result = P_THIS->SetVariableInternal(Variable, Type, TConstArrayView<uint8>(TempStorage, VariableProperty->GetElementSize()));
		}

		P_NATIVE_END;

		VariableProperty->DestroyValue(TempStorage);
	}

	switch (Result)
	{
	case EPropertyBagResult::Success:
		break;
	case EPropertyBagResult::TypeMismatch:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableTypeMismatch", "Incompatible type supplied for variable '{0}'"), FText::FromName(Variable.GetName()))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	case EPropertyBagResult::PropertyNotFound:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableNotFoundWarning", "Unknown variable name '{0}' supplied to Set Variable"), FText::FromName(Variable.GetName()))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	case EPropertyBagResult::OutOfBounds:
	case EPropertyBagResult::DuplicatedValue:
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::NonFatalError,
				FText::Format(NSLOCTEXT("UAFComponent", "UAFComponent_SetVariableUnknownWarning", "Unexpected internal error when calling Set Variable for '{0}'"), FText::FromName(Variable.GetName()))
			);

			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			break;
		}
	}
}

EPropertyBagResult UAnimNextComponent::SetVariableInternal(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InNewValue)
{
	if (Subsystem)
	{
		return Subsystem->SetVariable(this, InVariable, InType, InNewValue);
	}
	return EPropertyBagResult::PropertyNotFound;
}

EPropertyBagResult UAnimNextComponent::WriteVariableInternal(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TFunctionRef<void(TArrayView<uint8>)> InFunction)
{
	if (Subsystem)
	{
		return Subsystem->WriteVariable(this, InVariable, InType, InFunction);
	}
	return EPropertyBagResult::PropertyNotFound;
}

bool UAnimNextComponent::IsEnabled() const
{
	if (Subsystem)
	{
		return Subsystem->IsEnabled(this);
	}
	return false;
}

void UAnimNextComponent::SetEnabled(bool bEnabled)
{
	if(Subsystem)
	{
		Subsystem->SetEnabled(this, bEnabled);
	}
}

void UAnimNextComponent::ShowDebugDrawing(bool bInShowDebugDrawing)
{
#if UE_ENABLE_DEBUG_DRAWING
	bShowDebugDrawing = bInShowDebugDrawing;
	if(Subsystem)
	{
		Subsystem->ShowDebugDrawing(this, bInShowDebugDrawing);
	}
#endif
}

void UAnimNextComponent::QueueTask(FName InModuleEventName, TUniqueFunction<void(const UE::UAF::FModuleTaskContext&)>&& InTaskFunction, UE::UAF::ETaskRunLocation InLocation)
{
	using namespace UE::UAF;

	if(Subsystem)
	{
		Subsystem->QueueTask(this, InModuleEventName, MoveTemp(InTaskFunction), InLocation);
	}
}

void UAnimNextComponent::QueueInputTraitEvent(FAnimNextTraitEventPtr Event)
{
	using namespace UE::UAF;

	if(Subsystem)
	{
		Subsystem->QueueInputTraitEvent(this, Event);
	}
}

const FTickFunction* UAnimNextComponent::FindTickFunction(FName InEventName) const
{
	if(Subsystem)
	{
		return Subsystem->FindTickFunction(this, InEventName);
	}
	return nullptr;
}

void UAnimNextComponent::AddPrerequisite(UObject* InObject, FTickFunction& InTickFunction, FName InEventName)
{
	if(Subsystem)
	{
		Subsystem->AddDependency(this, InObject, InTickFunction, InEventName, UAnimNextWorldSubsystem::EDependency::Prerequisite);
	}
}

void UAnimNextComponent::AddComponentPrerequisite(UActorComponent* InComponent, FName InEventName)
{
	if (InComponent)
	{
		AddPrerequisite(InComponent, InComponent->PrimaryComponentTick, InEventName);
	}
}

void UAnimNextComponent::AddSubsequent(UObject* InObject, FTickFunction& InTickFunction, FName InEventName)
{
	if(Subsystem)
	{
		Subsystem->AddDependency(this, InObject, InTickFunction, InEventName, UAnimNextWorldSubsystem::EDependency::Subsequent);
	}
}

void UAnimNextComponent::AddComponentSubsequent(UActorComponent* InComponent, FName InEventName)
{
	if (InComponent)
	{
		AddSubsequent(InComponent, InComponent->PrimaryComponentTick, InEventName);
	}
}

void UAnimNextComponent::RemovePrerequisite(UObject* InObject, FTickFunction& InTickFunction, FName InEventName)
{
	if(Subsystem)
	{
		Subsystem->RemoveDependency(this, InObject, InTickFunction, InEventName, UAnimNextWorldSubsystem::EDependency::Prerequisite);
	}
}

void UAnimNextComponent::RemoveComponentPrerequisite(UActorComponent* InComponent, FName InEventName)
{
	if (InComponent)
	{
		RemovePrerequisite(InComponent, InComponent->PrimaryComponentTick, InEventName);
	}
}

void UAnimNextComponent::RemoveSubsequent(UObject* InObject, FTickFunction& InTickFunction, FName InEventName)
{
	if(Subsystem)
	{
		Subsystem->RemoveDependency(this, InObject, InTickFunction, InEventName, UAnimNextWorldSubsystem::EDependency::Subsequent);
	}
}

void UAnimNextComponent::RemoveComponentSubsequent(UActorComponent* InComponent, FName InEventName)
{
	if (InComponent)
	{
		RemoveSubsequent(InComponent, InComponent->PrimaryComponentTick, InEventName);
	}
}

void UAnimNextComponent::AddModuleEventPrerequisite(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName)
{
	if (!OtherAnimNextComponent)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAFComponent::AddModuleEventPrerequisite called with null OtherAnimNextComponent"));
	}
	else if (OtherAnimNextComponent == this)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAFComponent::AddModuleEventPrerequisite called using the same component"));
	}
	else if (Subsystem)
	{
		Subsystem->AddModuleEventDependency(this, InEventName, OtherAnimNextComponent, OtherEventName, UAnimNextWorldSubsystem::EDependency::Prerequisite);
	}
}

void UAnimNextComponent::AddModuleEventSubsequent(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName)
{
	if (!OtherAnimNextComponent)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAFComponent::AddModuleEventSubsequent called with null OtherAnimNextComponent"));
	}
	else if (OtherAnimNextComponent == this)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAFComponent::AddModuleEventPrerequisite called using the same component"));
	}
	else if (Subsystem)
	{
		Subsystem->AddModuleEventDependency(this, InEventName, OtherAnimNextComponent, OtherEventName, UAnimNextWorldSubsystem::EDependency::Subsequent);
	}
}

void UAnimNextComponent::RemoveModuleEventPrerequisite(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName)
{
	if (!OtherAnimNextComponent)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAnimNextComponent::RemoveModuleEventPrerequisite called with null OtherAnimNextComponent"));
	}
	else if (OtherAnimNextComponent == this)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAnimNextComponent::AddModuleEventPrerequisite called using the same component"));
	}
	else if (Subsystem)
	{
		Subsystem->RemoveModuleEventDependency(this, InEventName, OtherAnimNextComponent, OtherEventName, UAnimNextWorldSubsystem::EDependency::Prerequisite);
	}
}

void UAnimNextComponent::RemoveModuleEventSubsequent(FName InEventName, UAnimNextComponent* OtherAnimNextComponent, FName OtherEventName)
{
	if (!OtherAnimNextComponent)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAnimNextComponent::RemoveModuleEventSubsequent called with null OtherAnimNextComponent"));
	}
	else if (OtherAnimNextComponent == this)
	{
		UE_LOG(LogAnimation, Warning, TEXT("UAnimNextComponent::AddModuleEventPrerequisite called using the same component"));
	}
	else if (Subsystem)
	{
		Subsystem->RemoveModuleEventDependency(this, InEventName, OtherAnimNextComponent, OtherEventName, UAnimNextWorldSubsystem::EDependency::Subsequent);
	}
}

FAnimNextModuleHandle UAnimNextComponent::BlueprintGetModuleHandle() const
{
	return FAnimNextModuleHandle(ModuleHandle);
}

void UAnimNextComponent::SetModule(TObjectPtr<UAnimNextModule> InModule)
{
	Module = InModule;
}

void UAnimNextComponent::RegisterWithSubsystem()
{
	if (Subsystem && Module)
	{
		Subsystem->Register(this);
	}
}

void UAnimNextComponent::UnregisterWithSubsystem()
{
	if (Subsystem)
	{
		Subsystem->Unregister(this);
	}
}

bool UAnimNextComponent::IsModuleValid()
{
	return ModuleHandle.IsValid();
}

TObjectPtr<UAnimNextModule> UAnimNextComponent::GetModule() const
{
	return Module;
}
