// Copyright Epic Games, Inc. All Rights Reserved.


#include "RigUnit_EvaluateChooser.h"
#include "UAFAssetInstance.h"
#include "ControlRig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_EvaluateChooser)

static void RunChooserHelper(TConstArrayView<TObjectPtr<UObject>> ContextObjects, FStructView ContextStruct, TObjectPtr<UChooserTable> Chooser, TObjectPtr<UObject>& OutResult)
{
	OutResult = nullptr;
	
	if ((!ContextObjects.IsEmpty() || ContextStruct.IsValid()) && Chooser != nullptr)
	{
		FChooserEvaluationContext ChooserContext;
		for(TObjectPtr<UObject> ContextObject : ContextObjects)
		{
			if(ContextObject != nullptr)
			{
				ChooserContext.AddObjectParam(ContextObject);
			}
		}
		if(ContextStruct.IsValid())
		{
			ChooserContext.AddStructViewParam(ContextStruct);
		}

		UChooserTable::EvaluateChooser(ChooserContext, Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&OutResult](UObject* InResult)
		{
			OutResult = InResult;
			return FObjectChooserBase::EIteratorStatus::Stop;
		}));
	}
}

FRigUnit_EvaluateChooser_ControlRig_Execute()
{
	Result = nullptr;

	RunChooserHelper( { ContextObject, ExecuteContext.ControlRig }, FStructView(), Chooser, Result);
}

FRigUnit_EvaluateChooser_AnimNext_Execute()
{
	Result = nullptr;

	const FUAFAssetInstance& Instance = ExecuteContext.GetContextData<FAnimNextModuleContextData>().GetInstance();
	RunChooserHelper({ ContextObject }, FStructView::Make(const_cast<FUAFAssetInstance&>(Instance)), Chooser, Result);
}

const FName FRigVMDispatch_EvaluateChooser::ResultName = TEXT("Result");
const FName FRigVMDispatch_EvaluateChooser::ChooserName = TEXT("Chooser");
const FName FRigVMDispatch_EvaluateChooser::ContextObjectName = TEXT("ContextObject");

FRigVMDispatch_EvaluateChooser::FRigVMDispatch_EvaluateChooser()
{
	FactoryScriptStruct = StaticStruct();
}

FName FRigVMDispatch_EvaluateChooser::GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands, FRigVMRegistryHandle& InRegistry) const
{
	static const FName ArgumentNames[] =
	{
		ChooserName,
		ContextObjectName,
		ResultName,
	};
	check(InTotalOperands == UE_ARRAY_COUNT(ArgumentNames));
	return ArgumentNames[InOperandIndex];
}

const TArray<FRigVMTemplateArgumentInfo>& FRigVMDispatch_EvaluateChooser::GetArgumentInfos(FRigVMRegistryHandle& InRegistry) const
{
	if(CachedArgumentInfos.IsEmpty())
	{
		static const TArray<FRigVMTemplateArgument::ETypeCategory> ResultCategories =
		{
			FRigVMTemplateArgument::ETypeCategory_SingleObjectValue,
			FRigVMTemplateArgument::ETypeCategory_ArrayObjectValue
		};
		
		const TRigVMTypeIndex ChooserTypeIndex = InRegistry->GetTypeIndex_NoLock<UChooserTable>();
		const TRigVMTypeIndex ObjectTypeIndex = InRegistry->GetTypeIndex_NoLock<UObject>();
		CachedArgumentInfos.Emplace(ChooserName, ERigVMPinDirection::Input, ChooserTypeIndex);
		CachedArgumentInfos.Emplace(ContextObjectName, ERigVMPinDirection::Input, ObjectTypeIndex);
		CachedArgumentInfos.Emplace(ResultName, ERigVMPinDirection::Output, ResultCategories);
	}

	return CachedArgumentInfos;
}

void FRigVMDispatch_EvaluateChooser::Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches)
{

	const FProperty* ResultProperty = Handles[2].GetResolvedProperty();
	
	TObjectPtr<UChooserTable>* Chooser = reinterpret_cast<TObjectPtr<UChooserTable>*>(Handles[0].GetOutputData());
	TObjectPtr<UObject>* ContextObject = reinterpret_cast<TObjectPtr<UObject>*>(Handles[1].GetOutputData());


	FChooserEvaluationContext ChooserContext;
	if (InContext.GetContextPublicDataStruct()->IsChildOf(FAnimNextExecuteContext::StaticStruct()))
	{
		FAnimNextExecuteContext& VMExecuteContext = InContext.GetPublicData<FAnimNextExecuteContext>();
		const FUAFAssetInstance& Instance = VMExecuteContext.GetContextData<FAnimNextModuleContextData>().GetInstance();
		ChooserContext.AddStructViewParam(	FStructView::Make(const_cast<FUAFAssetInstance&>(Instance)));
	}
	else
	{
		FControlRigExecuteContext& ControlRigContext = InContext.GetPublicDataSafe<FControlRigExecuteContext>();
		ChooserContext.AddObjectParam(ControlRigContext.ControlRig);
	}
	ChooserContext.AddObjectParam(*ContextObject);

	if (const FArrayProperty* ArrayResultProperty = CastField<FArrayProperty>(ResultProperty))
	{
		if (const FObjectProperty* ObjectResultProperty = CastField<FObjectProperty>(ArrayResultProperty->Inner))
		{
			UClass* ObjectClass = ObjectResultProperty->PropertyClass;
			FScriptArrayHelper ArrayHelper(ArrayResultProperty, Handles[2].GetOutputData());
			ArrayHelper.Resize(0);
			
			UChooserTable::EvaluateChooser(ChooserContext, *Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([ObjectClass, &ArrayHelper](UObject* InResult)
			{
				if (InResult)
				{
					if (InResult->GetClass()->IsChildOf(ObjectClass))
					{
						int Index = ArrayHelper.AddValue();
						TObjectPtr<UObject>* Result = (TObjectPtr<UObject>*)ArrayHelper.GetElementPtr(Index);
						*Result = InResult;
						return Index == 0 ? FObjectChooserBase::EIteratorStatus::ContinueWithOutputs : FObjectChooserBase::EIteratorStatus::Continue;
					}
				}
				return FObjectChooserBase::EIteratorStatus::Continue;
			}));
		}
	}
	else if (const FObjectProperty* ObjectResultProperty = CastField<FObjectProperty>(ResultProperty))
	{
		UClass* ObjectClass = ObjectResultProperty->PropertyClass;
		TObjectPtr<UObject>* Result = (TObjectPtr<UObject>*)Handles[2].GetOutputData();
		*Result = nullptr;
		UChooserTable::EvaluateChooser(ChooserContext, *Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([ObjectClass, &Result](UObject* InResult)
		{
			if (InResult)
			{
				if (InResult->GetClass()->IsChildOf(ObjectClass))
				{
					*Result = InResult;
					return FObjectChooserBase::EIteratorStatus::Stop;
				}
			}
			
			return FObjectChooserBase::EIteratorStatus::Continue;
		}));
	}
}

