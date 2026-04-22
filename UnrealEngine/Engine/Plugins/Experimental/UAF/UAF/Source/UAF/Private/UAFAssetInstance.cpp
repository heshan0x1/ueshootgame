// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFAssetInstance.h"

#include "ObjectTrace.h"
#include "Logging/StructuredLog.h"
#include "Variables/UAFInstanceVariableContainer.h"
#include "Variables/VariableOverrides.h"
#include "Variables/VariableOverridesCollection.h"
#include "UAFAssetInstanceComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFAssetInstance)

void FUAFAssetInstance::InitializeVariables(const TSharedPtr<const UE::UAF::FVariableOverridesCollection>& InOverrides)
{
	Variables.Initialize(*this, InOverrides);
}

#if WITH_EDITOR
void FUAFAssetInstance::MigrateVariables()
{
	Variables.Migrate(*this);
}
#endif


void FUAFAssetInstance::ReleaseComponents()
{
	ComponentMap.Empty();
	CachedRigVMComponent = nullptr;
}

FUAFAssetInstance::FUAFAssetInstance()
{
#if ANIMNEXT_TRACE_ENABLED
	UniqueId = FObjectTrace::AllocateInstanceId();
#endif
}

#if DO_CHECK
bool FUAFAssetInstance::LayoutMatches(const FInstancedPropertyBag& InPropertyBag) const
{
	const UPropertyBag* PropertyBagStruct = InPropertyBag.GetPropertyBagStruct();
	if (PropertyBagStruct == nullptr)
	{
		return Variables.NumInternalVariables == 0;
	}

	if (Variables.NumInternalVariables != InPropertyBag.GetNumPropertiesInBag())
	{
		return false;
	}

	bool bMatches = true;
	TConstArrayView<FPropertyBagPropertyDesc> Descs = PropertyBagStruct->GetPropertyDescs();
	Variables.ForEachVariable([&Descs, &bMatches](FName InName, const FAnimNextParamType& InType, int32 InVariableIndex)
	{
		if (!Descs.IsValidIndex(InVariableIndex))
		{
			bMatches = false;
			return false;
		}

		const FPropertyBagPropertyDesc& Desc = Descs[InVariableIndex];
		if (Desc.Name != InName)
		{
			bMatches = false;
			return false;
		}

		if (Desc.ValueType != InType.GetValueType() ||
			Desc.ContainerTypes.GetFirstContainerType() != InType.GetContainerType() ||
			Desc.ValueTypeObject != InType.GetValueTypeObject())
		{
			bMatches = false;
			return false;
		}

		return true;
	});

	return bMatches;
}
#endif

FUAFAssetInstanceComponent* FUAFAssetInstance::TryGetComponent(const UScriptStruct* InStruct)
{
	checkf(InStruct->IsChildOf<FUAFAssetInstanceComponent>(), TEXT("ComponentType type must derive from FUAFAssetInstanceComponent"));

	const int32 ComponentStructHash = GetTypeHash(InStruct);

	if (TInstancedStruct<FUAFAssetInstanceComponent>* InstanceComponent = ComponentMap.FindByHash(ComponentStructHash, InStruct))
	{
		return InstanceComponent->GetMutablePtr<FUAFAssetInstanceComponent>();
	}
	return nullptr;
}
