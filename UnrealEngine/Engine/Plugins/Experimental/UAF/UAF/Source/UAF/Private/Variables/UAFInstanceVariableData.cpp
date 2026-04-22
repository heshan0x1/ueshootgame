// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/UAFInstanceVariableData.h"
#include "AnimNextRigVMAsset.h"
#include "UAFAssetInstance.h"
#include "Variables/StructDataCache.h"
#include "VariableOverrides.h"
#include "Logging/StructuredLog.h"
#include "Variables/AnimNextVariableReference.h"
#include "Variables/AnimNextSharedVariables.h"
#include "VariableOverridesCollection.h"
#include "Variables/UAFInstanceVariableContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFInstanceVariableData)

void FUAFInstanceVariableData::Initialize(const FUAFAssetInstance& InInstance, const TSharedPtr<const UE::UAF::FVariableOverridesCollection>& InOverrides)
{
	using namespace UE::UAF;

	Reset();

	// Add the primary asset's variables
	AddVariablesContainerForAsset(InInstance.GetAsset<UAnimNextRigVMAsset>(), InInstance, InOverrides);

	// Add referenced asset variables
	for (const UAnimNextRigVMAsset* ReferencedAsset : InInstance.GetAsset<UAnimNextRigVMAsset>()->GetReferencedVariableAssets())
	{
		AddVariablesContainerForAsset(ReferencedAsset, InInstance, InOverrides);
	}

	// Add referenced struct variables
	for (const UScriptStruct* Struct : InInstance.GetAsset<UAnimNextRigVMAsset>()->GetReferencedVariableStructs())
	{
		AddVariablesContainerForStruct(Struct, InInstance, InOverrides);
	}

	// Now any overrides we havent already added
	if (InOverrides.IsValid())
	{
		for (int32 OverrideIndex = 0; OverrideIndex < InOverrides->Collection.Num(); ++OverrideIndex)
		{
			const FVariableOverrides& Override = InOverrides->Collection[OverrideIndex];
			if (Override.AssetOrStructData.IsType<FVariableOverrides::FStructType>())
			{
				const UScriptStruct* Struct = Override.AssetOrStructData.Get<FVariableOverrides::FStructType>();
				if (AllSharedVariableContainersMap.Contains(Struct))
				{
					continue;
				}

				TSharedPtr<FUAFInstanceVariableContainer> NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, Struct, InOverrides, OverrideIndex);
				OwnedVariableContainers.Add(NewVariables.ToSharedRef());
				const int32 ReferencedAssetIndex = AllSharedVariableContainers.Emplace(NewVariables);
				// NOTE: not adding to InternalSharedVariableContainers, as these arent needed for running the graph
				AllSharedVariableContainersMap.Add(Struct, ReferencedAssetIndex);
				NumAllVariables += NewVariables->NumVariables;
			}
			else if(Override.AssetOrStructData.IsType<FVariableOverrides::FAssetType>())
			{
				const UAnimNextRigVMAsset* Asset = Override.AssetOrStructData.Get<FVariableOverrides::FAssetType>();
				if (AllSharedVariableContainersMap.Contains(Asset))
				{
					continue;
				}

				TSharedPtr<FUAFInstanceVariableContainer> NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, Asset, InOverrides, OverrideIndex);
				OwnedVariableContainers.Add(NewVariables.ToSharedRef());
				const int32 ReferencedAssetIndex = AllSharedVariableContainers.Emplace(NewVariables);
				// NOTE: not adding to InternalSharedVariableContainers, as these arent needed for running the graph
				AllSharedVariableContainersMap.Add(Asset, ReferencedAssetIndex);
				NumAllVariables += NewVariables->NumVariables;
			}
		}
	}

	RebuildNameMaps();
	
	ResolveOverrides();

#if WITH_EDITOR
	bHasBeenInitialized = true;
#endif
}

void FUAFInstanceVariableData::AddVariablesContainerForAsset(const UAnimNextRigVMAsset* InAsset, const FUAFAssetInstance& InInstance, const TSharedPtr<const UE::UAF::FVariableOverridesCollection>& InOverrides)
{
	using namespace UE::UAF;

	TSharedPtr<FUAFInstanceVariableContainer> NewVariables;

	// First check to see we have an override
	if(InOverrides.IsValid())
	{
		for (int32 OverrideIndex = 0; OverrideIndex < InOverrides->Collection.Num(); ++OverrideIndex)
		{
			const FVariableOverrides& Override = InOverrides->Collection[OverrideIndex];
			if (Override.IsOverrideFor(InAsset))
			{
				NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, InAsset, InOverrides, OverrideIndex);
				OwnedVariableContainers.Add(NewVariables.ToSharedRef());
				break;
			}
		}
	}

	// Check to see if this asset exists in our host chain. If so, we take a reference.
	if (!NewVariables.IsValid())
	{
		const FUAFAssetInstance* CurrentHostInstance = InInstance.GetHost();
		while (CurrentHostInstance)
		{
			if (const int32* SetIndexPtr = CurrentHostInstance->Variables.AllSharedVariableContainersMap.Find(InAsset))
			{
				NewVariables = CurrentHostInstance->Variables.AllSharedVariableContainers[*SetIndexPtr].Pin();
				break;
			}
			CurrentHostInstance = CurrentHostInstance->GetHost();
		}
	}

	// Otherwise, create a new instance that we own
	if (!NewVariables.IsValid())
	{
		NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, InAsset);
		OwnedVariableContainers.Add(NewVariables.ToSharedRef());
	}

	const int32 ReferencedAssetIndex = AllSharedVariableContainers.Emplace(NewVariables);
	InternalSharedVariableContainers.Add(NewVariables);
	AllSharedVariableContainersMap.Add(InAsset, ReferencedAssetIndex);
	NumAllVariables += NewVariables->NumVariables;
	NumInternalVariables += NewVariables->NumVariables;
}

void FUAFInstanceVariableData::AddVariablesContainerForStruct(const UScriptStruct* InStruct, const FUAFAssetInstance& InInstance, const TSharedPtr<const UE::UAF::FVariableOverridesCollection>& InOverrides)
{
	using namespace UE::UAF;

	TSharedPtr<FUAFInstanceVariableContainer> NewVariables;

	// First check to see we have an override
	if(InOverrides.IsValid())
	{
		for (int32 OverrideIndex = 0; OverrideIndex < InOverrides->Collection.Num(); ++OverrideIndex)
		{
			const FVariableOverrides& Override = InOverrides->Collection[OverrideIndex];
			if (Override.IsOverrideFor(InStruct))
			{
				NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, InStruct, InOverrides, OverrideIndex);
				OwnedVariableContainers.Add(NewVariables.ToSharedRef());
				break;
			}
		}
	}

	// Check to see if this asset exists in our host chain. If so, we take a reference.
	if (!NewVariables.IsValid())
	{
		const FUAFAssetInstance* CurrentHostInstance = InInstance.GetHost();
		while (CurrentHostInstance)
		{
			if (const int32* SetIndexPtr = CurrentHostInstance->Variables.AllSharedVariableContainersMap.Find(InStruct))
			{
				NewVariables = CurrentHostInstance->Variables.AllSharedVariableContainers[*SetIndexPtr].Pin();
				break;
			}
			CurrentHostInstance = CurrentHostInstance->GetHost();
		}
	}

	// Otherwise, create a new instance
	if (!NewVariables.IsValid())
	{
		NewVariables = MakeShared<FUAFInstanceVariableContainer>(InInstance, InStruct);
		OwnedVariableContainers.Add(NewVariables.ToSharedRef());
	}

	const int32 ReferencedAssetIndex = AllSharedVariableContainers.Emplace(NewVariables);
	InternalSharedVariableContainers.Add(NewVariables);
	AllSharedVariableContainersMap.Add(InStruct, ReferencedAssetIndex);
	NumAllVariables += NewVariables->NumVariables;
	NumInternalVariables += NewVariables->NumVariables;
}

void FUAFInstanceVariableData::RebuildNameMaps()
{
	using namespace UE::UAF;

	LegacyVariableNameMap.Reset();
	VariablesMap.Reset();

	// Setup name maps
	const int32 NumSharedVariableSets = AllSharedVariableContainers.Num();
	for (int32 SharedVariableSetIndex = 0; SharedVariableSetIndex < NumSharedVariableSets; ++SharedVariableSetIndex)
	{
		const TSharedRef<FUAFInstanceVariableContainer>& VariableSet = AllSharedVariableContainers[SharedVariableSetIndex].Pin().ToSharedRef();
		switch (VariableSet->VariablesContainer.GetIndex())
		{
		case FUAFInstanceVariableContainer::FVariablesContainerType::IndexOfType<FInstancedPropertyBag>():
			{
				const UAnimNextRigVMAsset* Asset = VariableSet->AssetOrStructData.Get<FUAFInstanceVariableContainer::FAssetType>().Get();
				const int32 NumVariablesInSet = VariableSet->NumVariables;
				if (NumVariablesInSet > 0)
				{
					const FInstancedPropertyBag& PropertyBag = VariableSet->VariablesContainer.Get<FInstancedPropertyBag>();
					TConstArrayView<FPropertyBagPropertyDesc> Descs = PropertyBag.GetPropertyBagStruct()->GetPropertyDescs();
					check(Descs.Num() == NumVariablesInSet);
					for (int32 VariableIndex = 0; VariableIndex < NumVariablesInSet; ++VariableIndex)
					{
						FUAFInstanceVariableData::FVariableMapping Mapping(SharedVariableSetIndex, VariableIndex);
						VariablesMap.Add(FAnimNextVariableReference(Descs[VariableIndex].Name, Asset), Mapping);
						LegacyVariableNameMap.Add(Descs[VariableIndex].Name, Mapping);
					}
				}
				break;
			}
		case FUAFInstanceVariableContainer::FVariablesContainerType::IndexOfType<FInstancedStruct>():
			{
				const int32 NumVariablesInSet = VariableSet->NumVariables;
				if (NumVariablesInSet > 0)
				{
					const UScriptStruct* Struct = VariableSet->AssetOrStructData.Get<FUAFInstanceVariableContainer::FStructType>().Get();
					TConstArrayView<FStructDataCache::FPropertyInfo> Properties = VariableSet->StructDataCache->GetProperties();
					check(Properties.Num() == NumVariablesInSet);
					for (int32 VariableIndex = 0; VariableIndex < NumVariablesInSet; ++VariableIndex)
					{
						FName Name = Properties[VariableIndex].Property->GetFName();
						FUAFInstanceVariableData::FVariableMapping Mapping(SharedVariableSetIndex, VariableIndex);
						VariablesMap.Add(FAnimNextVariableReference(Name, Struct), Mapping);
						LegacyVariableNameMap.Add(Name, Mapping);
					}
				}
				break;
			}
		default:
			checkNoEntry();
			break;
		}
	}
}

void FUAFInstanceVariableData::Reset()
{
	OwnedVariableContainers.Reset();
	InternalSharedVariableContainers.Reset();
	AllSharedVariableContainers.Reset();
	AllSharedVariableContainersMap.Reset();
	LegacyVariableNameMap.Reset();
	VariablesMap.Reset();
	NumAllVariables = 0;
	NumInternalVariables = 0;
}

#if WITH_EDITOR
void FUAFInstanceVariableData::Migrate(const FUAFAssetInstance& InInstance)
{
	// Higher level logic needs to check to see if we have already been initialized, as overrides need to be cached first
	check(bHasBeenInitialized);

	const UAnimNextRigVMAsset* InstanceAsset = InInstance.GetAsset<UAnimNextRigVMAsset>();
	TConstArrayView<TObjectPtr<const UAnimNextRigVMAsset>> ReferencedVariableAssets = InstanceAsset->GetReferencedVariableAssets();
	TConstArrayView<TObjectPtr<const UScriptStruct>> ReferencedVariableStructs = InstanceAsset->GetReferencedVariableStructs();

	// Referenced assets + struct + self-container
	const int32 NumExpectedContainers = ReferencedVariableAssets.Num() + ReferencedVariableStructs.Num() + 1;
	const int32 NumExistingContainers = AllSharedVariableContainersMap.Num();

		// Verify the number of containers, new ones might have been added or existing ones removed
	const bool bMissingContainers = NumExpectedContainers > NumExistingContainers;
	const bool bOutOfDataContainers = NumExistingContainers > NumExpectedContainers;

	if (bMissingContainers)
	{
		// Referenced asset variables
		for (const UAnimNextRigVMAsset* ReferencedAsset : ReferencedVariableAssets)
		{
			const bool bHasVariableContainerForAsset = OwnedVariableContainers.ContainsByPredicate([ReferencedAsset](const TSharedRef<FUAFInstanceVariableContainer>& Container)
			{
				if (const FUAFInstanceVariableContainer::FAssetType* VariantPtr = Container->AssetOrStructData.TryGet<TObjectPtr<const UAnimNextRigVMAsset>>())
				{
					return (*VariantPtr) == ReferencedAsset;
				}
				return false;
			});
		
			if (!bHasVariableContainerForAsset)
			{
				AddVariablesContainerForAsset(ReferencedAsset, InInstance, nullptr);
			}
		}

		// Referenced struct variables
		for (const UScriptStruct* Struct : ReferencedVariableStructs)
		{
			const bool bHasVariableContainerForStruct = OwnedVariableContainers.ContainsByPredicate([Struct](const TSharedRef<FUAFInstanceVariableContainer>& Container)
			{
				if (const FUAFInstanceVariableContainer::FStructType* VariantPtr = Container->AssetOrStructData.TryGet<FUAFInstanceVariableContainer::FStructType>())
				{
					return (*VariantPtr) == Struct;
				}
				return false;
			});

			if (!bHasVariableContainerForStruct)
			{
				AddVariablesContainerForStruct(Struct, InInstance, nullptr);
			}
		}
	}
	else if (bOutOfDataContainers)
	{		
		for (int32 Index = 0; Index < AllSharedVariableContainers.Num(); ++Index)
		{
			const TWeakPtr<FUAFInstanceVariableContainer>& WeakContainer = AllSharedVariableContainers[Index];
			if (TSharedPtr<FUAFInstanceVariableContainer> SharedVariableContainer = WeakContainer.Pin())
			{
				const UObject* Object = nullptr;
				switch (SharedVariableContainer->AssetOrStructData.GetIndex())
				{
				case FUAFInstanceVariableContainer::FAssetOrStructType::IndexOfType<FUAFInstanceVariableContainer::FAssetType>():
					Object = SharedVariableContainer->AssetOrStructData.Get<FUAFInstanceVariableContainer::FAssetType>();
					break;
				case FUAFInstanceVariableContainer::FAssetOrStructType::IndexOfType<FUAFInstanceVariableContainer::FStructType>():
					Object = SharedVariableContainer->AssetOrStructData.Get<FUAFInstanceVariableContainer::FStructType>();
					break;
				default:
					checkNoEntry()
					break;
				}

				// Referenced shared asset, or object corresponds to the instanced asset
				const bool bIsAssetEntry = ReferencedVariableAssets.Contains(Object) || InstanceAsset == Object;
				const bool bIsStructEntry = ReferencedVariableStructs.Contains(Object);

				// Remove variable containers for asset/struct which are no longer referenced/part of this instance
				if (!bIsAssetEntry && !bIsStructEntry)
				{
					AllSharedVariableContainers.RemoveAtSwap(Index);

					OwnedVariableContainers.Remove(SharedVariableContainer.ToSharedRef());
					InternalSharedVariableContainers.Remove(WeakContainer);
					
					--Index;
				}
			}
		}
	}

	// Only migrate required/valid containers
	for (TSharedRef<FUAFInstanceVariableContainer>& OwnedVariableContainer : OwnedVariableContainers)
	{
		OwnedVariableContainer->Migrate();
	}

	NumAllVariables = 0;
	NumInternalVariables = 0;
	AllSharedVariableContainersMap.Reset();

	int32 ContainerIndex = 0;
	for (TWeakPtr<FUAFInstanceVariableContainer>& WeakSharedVariableContainer : AllSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> SharedVariableContainer = WeakSharedVariableContainer.Pin();
		check(SharedVariableContainer.IsValid());

		NumAllVariables += SharedVariableContainer->NumVariables;

		const UObject* Object = nullptr;
		switch (SharedVariableContainer->AssetOrStructData.GetIndex())
		{
		case FUAFInstanceVariableContainer::FAssetOrStructType::IndexOfType<FUAFInstanceVariableContainer::FAssetType>():
			Object = SharedVariableContainer->AssetOrStructData.Get<FUAFInstanceVariableContainer::FAssetType>();
			break;
		case FUAFInstanceVariableContainer::FAssetOrStructType::IndexOfType<FUAFInstanceVariableContainer::FStructType>():
			Object = SharedVariableContainer->AssetOrStructData.Get<FUAFInstanceVariableContainer::FStructType>();
			break;
		default:
			checkNoEntry()
			break;
		}

		AllSharedVariableContainersMap.Add(Object, ContainerIndex++);
	}

	RebuildNameMaps();

	for (TWeakPtr<FUAFInstanceVariableContainer>& WeakInternalSharedVariableContainer : InternalSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> InternalSharedVariableContainer = WeakInternalSharedVariableContainer.Pin();
		check(InternalSharedVariableContainer.IsValid());
		
		NumInternalVariables += InternalSharedVariableContainer->NumVariables;
	}
}
#endif

void FUAFInstanceVariableData::ResolveOverrides()
{
	for (TSharedRef<FUAFInstanceVariableContainer>& OwnedVariableContainer : OwnedVariableContainers)
	{
		OwnedVariableContainer->ResolveOverrides();
	}
}

const FUAFInstanceVariableData::FVariableMapping* FUAFInstanceVariableData::FindMapping(const FAnimNextVariableReference& InVariable) const
{
	const FVariableMapping* FoundMapping = nullptr;
	if (InVariable.GetObject() == nullptr)
	{
		// Legacy path, lookup by name only
		FoundMapping = LegacyVariableNameMap.Find(InVariable.GetName());
	}
	else
	{
		FoundMapping = VariablesMap.Find(InVariable);
	}
	return FoundMapping;
}

EPropertyBagResult FUAFInstanceVariableData::GetVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TArrayView<uint8> OutResult) const
{
	using namespace UE::UAF;

	const FVariableMapping* FoundMapping = FindMapping(InVariable);
	if (FoundMapping == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}

	const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet = AllSharedVariableContainers[FoundMapping->AllSharedVariableContainersIndex];
	TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
	check(PinnedVariableSet.IsValid());
	return PinnedVariableSet->GetVariable(FoundMapping->VariableIndex, InType, OutResult);
}

EPropertyBagResult FUAFInstanceVariableData::AccessVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TArrayView<uint8>& OutResult) const
{
	using namespace UE::UAF;

	const FVariableMapping* FoundMapping = FindMapping(InVariable);
	if (FoundMapping == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}

	const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet = AllSharedVariableContainers[FoundMapping->AllSharedVariableContainersIndex];
	TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
	check(PinnedVariableSet.IsValid());
	return PinnedVariableSet->AccessVariable(FoundMapping->VariableIndex, InType, OutResult);
}

EPropertyBagResult FUAFInstanceVariableData::SetVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InNewValue) const
{
	using namespace UE::UAF;

	const FVariableMapping* FoundMapping = FindMapping(InVariable);
	if (FoundMapping == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}

	const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet = AllSharedVariableContainers[FoundMapping->AllSharedVariableContainersIndex];
	TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
	check(PinnedVariableSet.IsValid());
	return PinnedVariableSet->SetVariable(FoundMapping->VariableIndex, InType, InNewValue);
}

bool FUAFInstanceVariableData::AccessVariablesStructInternal(const UScriptStruct* InStruct, TFunctionRef<void(FStructView)> InFunction) const
{
	const int32* FoundIndexPtr = AllSharedVariableContainersMap.Find(InStruct);
	if (FoundIndexPtr == nullptr)
	{
		return false;
	}

	TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = AllSharedVariableContainers[*FoundIndexPtr].Pin();
	check(PinnedVariableSet.IsValid());
	if (!PinnedVariableSet->VariablesContainer.IsType<FInstancedStruct>())
	{
		// Not an instanced struct (can be a set of overrides), so cant return a view
		return false;
	}

	InFunction(PinnedVariableSet->VariablesContainer.Get<FInstancedStruct>());
	return true;
}

void FUAFInstanceVariableData::ForEachVariablesStructInternal(TFunctionRef<void(FStructView)> InFunction) const
{
	for (const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet : AllSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
		check(PinnedVariableSet.IsValid());
		if (!PinnedVariableSet->VariablesContainer.IsType<FInstancedStruct>())
		{
			// Not an instanced struct (can be a set of overrides), so cant supply a view
			continue;
		}

		InFunction(PinnedVariableSet->VariablesContainer.Get<FInstancedStruct>());
	}
}

void FUAFInstanceVariableData::GetAllVariablesOfType(const FAnimNextParamType& InType, TArray<FAnimNextVariableReference>& OutVariables) const
{
	for (const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet : AllSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
		PinnedVariableSet->GetAllVariablesOfType(InType, OutVariables);
	}
}

void FUAFInstanceVariableData::ForEachVariable(TFunctionRef<bool(FName, const FAnimNextParamType&, int32)> InFunction) const
{
	int32 VariableIndex = 0;
	for (const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet : AllSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
		if (!PinnedVariableSet->ForEachVariable(InFunction, VariableIndex))
		{
			break;
		}
	}
}

void FUAFInstanceVariableData::AccessVariablePropertyByIndex(int32 InIndex, TFunctionRef<void(const FProperty*, TArrayView<uint8>)> InFunction)
{
	int32 VariableIndexOffset = 0;
	for (const TWeakPtr<FUAFInstanceVariableContainer>& SharedVariableSet : AllSharedVariableContainers)
	{
		TSharedPtr<FUAFInstanceVariableContainer> PinnedVariableSet = SharedVariableSet.Pin();
		const int32 BaseIndex = VariableIndexOffset;
		VariableIndexOffset += PinnedVariableSet->NumVariables;
		if (InIndex < VariableIndexOffset)
		{
			const FProperty* Property = nullptr;
			TArrayView<uint8> Data;
			PinnedVariableSet->AccessVariableUnchecked(InIndex - BaseIndex, Property, Data);
			InFunction(Property, Data);
			return;
		}
	}
}

void FUAFInstanceVariableData::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	for (const TSharedRef<FUAFInstanceVariableContainer>& SharedVariableSet : OwnedVariableContainers)
	{
		Collector.AddPropertyReferencesWithStructARO(FUAFInstanceVariableContainer::StaticStruct(), &SharedVariableSet.Get());
	}
}
