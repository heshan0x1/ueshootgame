// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextRigVMAsset.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif
#include "AnimNextFunctionHandle.h"
#include "AnimNextRigVMFunctionData.h"
#include "RigVMRuntimeDataRegistry.h"
#include "Graph/RigUnit_AnimNextBeginExecution.h"
#include "Modules/ModuleManager.h"
#include "UObject/AssetRegistryTagsContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextRigVMAsset)

namespace UE::UAF::Private
{
#if WITH_EDITOR
	static UAnimNextRigVMAsset::FOnCompileJobEvent GOnCompileJobStarted;
	static UAnimNextRigVMAsset::FOnCompileJobEvent GOnCompileJobFinished;
#endif
}

UAnimNextRigVMAsset::UAnimNextRigVMAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetRigVMExtendedExecuteContext(&ExtendedExecuteContext);
}

void UAnimNextRigVMAsset::BeginDestroy()
{
	Super::BeginDestroy();

	if (VM)
	{
		UE::UAF::FRigVMRuntimeDataRegistry::ReleaseAllVMRuntimeData(VM);
	}
}

void UAnimNextRigVMAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// External editor-side APIs assume that the editordata is also postloaded with the asset
	EditorData->ConditionalPreload();
	EditorData->ConditionalPostLoad();
#endif
	
	ExtendedExecuteContext.InvalidateCachedMemory();

	VM = RigVM;

	// In packaged builds, initialize the VM
	// In editor, the VM will be recompiled and initialized at UAnimNextRigVMAssetEditorData::HandlePackageDone::RecompileVM
#if !WITH_EDITOR
	if(VM != nullptr)
	{
		VM->ClearExternalVariables(ExtendedExecuteContext);
		VM->SetExternalVariableDefs(GetExternalVariablesImpl(false));
		VM->Initialize(ExtendedExecuteContext);
		InitializeVM(FRigUnit_AnimNextBeginExecution::EventName);
	}
#endif
}

void UAnimNextRigVMAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

#if WITH_EDITORONLY_DATA
	if(EditorData)
	{
		EditorData->GetAssetRegistryTags(Context);
	}
#endif

#if WITH_EDITOR
	// Allow asset user data to output tags
	for(const UAssetUserData* AssetUserDataItem : *GetAssetUserDataArray())
	{
		if (AssetUserDataItem)
		{
			AssetUserDataItem->GetAssetRegistryTags(Context);
		}
	}
#endif // WITH_EDITOR
}

void UAnimNextRigVMAsset::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);

#if WITH_EDITORONLY_DATA
	if (EditorData)
	{
		EditorData->PreDuplicate(DupParams);
	}
#endif
}

TArray<FRigVMExternalVariable> UAnimNextRigVMAsset::GetExternalVariablesImpl(bool bFallbackToBlueprint) const
{
	TArray<FRigVMExternalVariable> ExternalVariables;

	if(const UPropertyBag* PropertyBag = CombinedPropertyBag.GetPropertyBagStruct())
	{
		TConstArrayView<FPropertyBagPropertyDesc> VariableDescs = PropertyBag->GetPropertyDescs();
		if(VariableDescs.Num() != 0)
		{
			uint8* Container = const_cast<uint8*>(CombinedPropertyBag.GetValue().GetMemory());
			ExternalVariables.Reserve(ExternalVariables.Num() + VariableDescs.Num());
			for(const FPropertyBagPropertyDesc& Desc : VariableDescs)
			{
				const FProperty* Property = Desc.CachedProperty;
				FRigVMExternalVariable ExternalVariable = FRigVMExternalVariable::Make(Property, Container);
				if(!ExternalVariable.IsValid())
				{
					UE_LOG(LogRigVM, Error, TEXT("%s: Property '%s' of type '%s' is not supported."), *GetName(), *Property->GetName(), *Property->GetCPPType());
					continue;
				}

				ExternalVariables.Add(ExternalVariable);
			}
		}
	}

	return ExternalVariables;
}


void UAnimNextRigVMAsset::CallFunctionHandle(UE::UAF::FFunctionHandle InHandle, FRigVMExtendedExecuteContext& InContext, FInstancedPropertyBag& InArgs)
{
#if WITH_EDITOR
	check(InHandle.IsValidForVM(VM->GetVMHash()));
#endif

	const FAnimNextRigVMFunctionData& Function = FunctionData[InHandle.FunctionIndex];
	const int32 NumArgs = Function.ArgIndices.Num();
	check(InArgs.GetNumPropertiesInBag() == NumArgs);

	const UPropertyBag* PropertyBag = InArgs.GetPropertyBagStruct();
	if (PropertyBag)
	{
		// Copy-in args
		void* ContainerPtr = InArgs.GetMutableValue().GetMemory();
		TConstArrayView<FPropertyBagPropertyDesc> Descs = PropertyBag->GetPropertyDescs();
		for (int32 ArgIndex = 0; ArgIndex < NumArgs; ++ArgIndex)
		{
			const FPropertyBagPropertyDesc& Desc = Descs[ArgIndex];
			const int32 ExternalVariableIndex = Function.ArgIndices[ArgIndex];
			check(VM->GetExternalVariableDefs()[ExternalVariableIndex].Property->GetClass() == Desc.CachedProperty->GetClass());
			void* DestPtr = InContext.ExternalVariableRuntimeData[ExternalVariableIndex].Memory;
			void* SrcPtr = Desc.CachedProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
			Desc.CachedProperty->CopyCompleteValue(DestPtr, SrcPtr);
		}

		// Run the function's wrapper event
		VM->ExecuteVM(InContext, Function.EventName);

		// Copy-out args
		for (int32 ArgIndex = 0; ArgIndex < NumArgs; ++ArgIndex)
		{
			const FPropertyBagPropertyDesc& Desc = Descs[ArgIndex];
			void* DestPtr = Desc.CachedProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
			void* SrcPtr = InContext.ExternalVariableRuntimeData[Function.ArgIndices[ArgIndex]].Memory;
			Desc.CachedProperty->CopyCompleteValue(DestPtr, SrcPtr);
		}
	}
	else
	{
		// No args - just run the function's wrapper event
		VM->ExecuteVM(InContext, Function.EventName);
	}
}

UE::UAF::FFunctionHandle UAnimNextRigVMAsset::GetFunctionHandle(FName InEventName) const
{
	using namespace UE::UAF;

	for (int32 FunctionIndex = 0; FunctionIndex < FunctionData.Num(); ++FunctionIndex)
	{
		const FAnimNextRigVMFunctionData& Function = FunctionData[FunctionIndex];
		if (Function.EventName == InEventName)
		{
			FFunctionHandle Handle;
			Handle.FunctionIndex = FunctionIndex;
#if WITH_EDITOR
			Handle.VMHash = VM->GetVMHash();
#endif
			return Handle;
		}
	}

	return FFunctionHandle();
}

#if WITH_EDITOR

UAnimNextRigVMAsset::FOnCompileJobEvent& UAnimNextRigVMAsset::OnCompileJobStarted()
{
	return UE::UAF::Private::GOnCompileJobStarted;
}

UAnimNextRigVMAsset::FOnCompileJobEvent& UAnimNextRigVMAsset::OnCompileJobFinished()
{
	return UE::UAF::Private::GOnCompileJobFinished;
}

#endif
