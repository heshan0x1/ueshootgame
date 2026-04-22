// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/AnimNextStateTreeRigVMConditionBase.h"
#include "AnimNextExecuteContext.h"
#include "Graph/AnimNextGraphContextData.h"
#include "Graph/AnimNextGraphInstance.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "StructUtils/PropertyBag.h"
#include "TraitCore/ExecutionContext.h"
#include "AnimNextStateTreeContext.h"

#if WITH_EDITOR
#include "AnimNextStateTreeEditorOnlyTypes.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "RigVMFunctions/RigVMFunctionDefines.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "UncookedOnlyUtils.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AnimNextStateTreeRigVMConditionBase"

#include "UAFRigVMComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextStateTreeRigVMConditionBase)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FAnimNextStateTreeRigVMConditionBase

bool FAnimNextStateTreeRigVMConditionBase::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TraitContextHandle);
	return true;
}

bool FAnimNextStateTreeRigVMConditionBase::TestCondition(FStateTreeExecutionContext& Context) const
{
	if (FAnimNextStateTreeRigVMConditionInstanceData* InstanceData = Context.GetInstanceDataPtr<FAnimNextStateTreeRigVMConditionInstanceData>(*this))
	{
		FAnimNextStateTreeTraitContext& ExecContext = Context.GetExternalData(TraitContextHandle);
		FAnimNextGraphInstance& GraphInstance = ExecContext.GetGraphInstance();

		// Need const cast as VM execution isn't const.
		if (UAnimNextRigVMAsset* RigVMInstance = const_cast<UAnimNextRigVMAsset*>(GraphInstance.GetAsset<UAnimNextRigVMAsset>()))
		{
#if WITH_EDITOR
			if (!InstanceData->FunctionHandle.IsValidForVM(RigVMInstance->GetRigVM()->GetVMHash()))
#else
			if (!InstanceData->FunctionHandle.IsValid())
#endif
			{
				InstanceData->FunctionHandle = RigVMInstance->GetFunctionHandle(InternalEventName);
			}

			if (!ensure(InstanceData->FunctionHandle.IsValid() && ResultIndex != INDEX_NONE))
			{
				return false;
			}

			FUAFRigVMComponent& RigVMComponent = GraphInstance.GetComponent<FUAFRigVMComponent>();
			FRigVMExtendedExecuteContext& ExtendedExecuteContext = RigVMComponent.GetExtendedExecuteContext();
			FAnimNextExecuteContext& AnimNextContext = ExtendedExecuteContext.GetPublicDataSafe<FAnimNextExecuteContext>();

			FAnimNextGraphContextData ContextData(GraphInstance.GetModuleInstance(), &GraphInstance);
			UE::UAF::FScopedExecuteContextData ContextDataScope(AnimNextContext, ContextData);

			RigVMInstance->CallFunctionHandle(InstanceData->FunctionHandle, ExtendedExecuteContext, InstanceData->Parameters);

			// Pull the result out of the property bag
			return InstanceData->Parameters.GetValueBool(InstanceData->Parameters.GetPropertyBagStruct()->GetPropertyDescs()[ResultIndex]).GetValue();
		}
	}

	return true;
}

#if WITH_EDITOR
FText FAnimNextStateTreeRigVMConditionBase::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return LOCTEXT("AnimNextStateTreeConditon_Desc", "RigVM function driven condition");
}

void FAnimNextStateTreeRigVMConditionBase::PostEditNodeChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView)
{
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(FAnimNextStateTreeRigVMConditionBase, ConditionFunctionName))
	{
		// Function Name selected has changed. Update param struct & result / event names used during execution.
		if (FAnimNextStateTreeRigVMConditionInstanceData* InstanceData = InstanceDataView.GetMutablePtr<FAnimNextStateTreeRigVMConditionInstanceData>())
		{
			// @TODO: This relies on the function name being unique (Ex: In a workspace). For now that's okay. Later on however we will want to use a more robust function picker
			auto GetRigVMFunctionHeader = [](FName InName)
			{
				FRigVMGraphFunctionHeader Result = FRigVMGraphFunctionHeader();

				TMap<FAssetData, FRigVMGraphFunctionHeaderArray> FunctionExports;
				UE::UAF::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::UAF::AnimNextPublicGraphFunctionsExportsRegistryTag, FunctionExports);
				UE::UAF::UncookedOnly::FUtils::GetExportedFunctionsFromAssetRegistry(UE::UAF::ControlRigAssetPublicGraphFunctionsExportsRegistryTag, FunctionExports);

				for (const auto& Export : FunctionExports.Array())
				{
					for (const FRigVMGraphFunctionHeader& FunctionHeader : Export.Value.Headers)
					{
						if (InName == FunctionHeader.Name)
						{
							Result = FunctionHeader;
							return Result;
						}
					}
				}

				return Result;
			};

			RigVMFunctionHeader = GetRigVMFunctionHeader(ConditionFunctionName);
			InstanceData->Parameters.Reset();
			ResultIndex = INDEX_NONE;

			TArray<FPropertyBagPropertyDesc> PropertyDescs;
			PropertyDescs.Reserve(RigVMFunctionHeader.Arguments.Num());
			for (const FRigVMGraphFunctionArgument& Argument : RigVMFunctionHeader.Arguments)
			{
				if (Argument.Direction == ERigVMPinDirection::Input || Argument.Direction == ERigVMPinDirection::Output)
				{
					FString CppTypeString = Argument.CPPType.ToString();
					UObject* CppTypeObject = Argument.CPPTypeObject.Get();
					FPropertyBagPropertyDesc PropertyDesc = FRigVMMemoryStorageStruct::GeneratePropertyBagDescriptor(FRigVMPropertyDescription(Argument.Name, CppTypeString, CppTypeObject, Argument.DefaultValue));
					if (Argument.Direction == ERigVMPinDirection::Output && Argument.CPPType == RigVMTypeUtils::BoolTypeName)
					{
						PropertyDesc.PropertyFlags &= ~CPF_Edit;	// Hide the result from the UI
						ResultIndex = PropertyDescs.Num();
					}
					PropertyDescs.Add(PropertyDesc);
				}
			}

			// TODO: Constrain functions we can pick to those returning only a bool
			ensure(ResultIndex != INDEX_NONE);

			InstanceData->Parameters.AddProperties(PropertyDescs);
		}
	}
}

void FAnimNextStateTreeRigVMConditionBase::GetProgrammaticFunctionHeaders(FAnimNextStateTreeProgrammaticFunctionHeaderParams& InProgrammaticFunctionHeaderParams, const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc)
{
	using namespace UE::UAF::UncookedOnly;

	FAnimNextGetFunctionHeaderCompileContext& OutCompileContext = InProgrammaticFunctionHeaderParams.OutCompileContext;
	
	StateName = State->Name;
	NodeId = Desc.ID;
	InternalEventName = FName(FUtils::MakeFunctionWrapperEventName(RigVMFunctionHeader.Name));

	FAnimNextProgrammaticFunctionHeader AnimNextFunctionHeader = {};
	AnimNextFunctionHeader.FunctionHeader = RigVMFunctionHeader;
	OutCompileContext.AddUniqueFunctionHeader(AnimNextFunctionHeader);
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
