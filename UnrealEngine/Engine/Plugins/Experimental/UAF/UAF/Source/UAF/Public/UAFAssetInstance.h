// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UAFAssetInstanceComponent.h"
#include "Param/ParamType.h"
#include "StructUtils/PropertyBag.h"
#include "RewindDebugger/AnimNextTrace.h"
#include "Variables/UAFInstanceVariableData.h"
#include "UAFAssetInstance.generated.h"

#define UE_API UAF_API

class UAnimNextSharedVariables;
struct FAnimNextModuleInjectionComponent;
struct FUAFAssetInstanceComponent;
struct FUAFRigVMComponent;

namespace UE::UAF
{
	struct FPoolHandle;
	struct FInjectionInfo;
	struct FAnimNextTrace;
	struct FInstanceTaskContext;
	struct FVariableOverridesCollection;
}

// Base struct for instances of UAF assets
USTRUCT()
struct FUAFAssetInstance
{
	GENERATED_BODY()

	UE_API FUAFAssetInstance();

	// Get the asset that this instance represents
	template<typename AssetType>
	const AssetType* GetAsset() const
	{
		return CastChecked<AssetType>(Asset);
	}

	// Safely get the name of the asset that this host provides
	FName GetAssetName() const
	{
		return Asset ? Asset->GetFName() : NAME_None;
	}

	// Get a variable's value given its name, copying the value to OutResult.
	// If the type does not match exactly then a conversion will be attempted.
	// @param	InVariable			The variable to get the value of
	// @param	OutResult			Result that will be filled if no errors occur
	// @return see EPropertyBagResult
	template<typename ValueType>
	EPropertyBagResult GetVariable(const FAnimNextVariableReference& InVariable, ValueType& OutResult) const
	{
		return Variables.GetVariable(InVariable, FAnimNextParamType::GetType<ValueType>(), TArrayView<uint8>(reinterpret_cast<uint8*>(&OutResult), sizeof(ValueType)));
	}

	// Access a variable's value given its name.
	// Type must match strictly, no conversions are performed.
	// @param	InVariable			The variable to get the value of
	// @param	InFunction			Function that will be called if no errors occur
	// @return see EPropertyBagResult
	template<typename ValueType>
	EPropertyBagResult AccessVariable(const FAnimNextVariableReference& InVariable, TFunctionRef<void(ValueType&)> InFunction) const
	{
		TArrayView<uint8> Data;
		EPropertyBagResult Result = Variables.AccessVariable(InVariable, FAnimNextParamType::GetType<ValueType>(), Data);
		if (Result == EPropertyBagResult::Success)
		{
			InFunction(*reinterpret_cast<ValueType*>(Data.GetData()));
		}
		return Result;
	}
	
	// Set a variable's value given a reference.
	// If the type does not match exactly then a conversion will be attempted.
	// @param	InVariable			The variable to set the value of
	// @param	InNewValue			The value to set the variable to
	// @return see EPropertyBagResult
	template<typename ValueType>
	EPropertyBagResult SetVariable(const FAnimNextVariableReference& InVariable, const ValueType& InNewValue) const
	{
		return SetVariable(InVariable, FAnimNextParamType::GetType<ValueType>(), TConstArrayView<uint8>(reinterpret_cast<const uint8*>(&InNewValue), sizeof(ValueType)));
	}

	// Set a variable's value given a reference.
	// If the type does not match exactly then a conversion will be attempted.
	// @param	InVariable			The variable to set the value of
	// @param	InType				The type of the variable
	// @param	InNewValue			The value to set the variable to
	// @return see EPropertyBagResult
	EPropertyBagResult SetVariable(const FAnimNextVariableReference& InVariable, const FAnimNextParamType& InType, TConstArrayView<uint8> InNewValue) const
	{
		return Variables.SetVariable(InVariable, InType, InNewValue);
	}

	// Access the memory of the shared variable struct directly.
	// @param	InFunction			Function called with a reference to the variable's struct 
	// @return true if the variables could be accessed. Variables can exist but be unable to be accessed if user overrides are set
	template<typename StructType>
	bool AccessVariablesStruct(TFunctionRef<void(StructType&)> InFunction) const
	{
		return Variables.AccessVariablesStructInternal(TBaseStructure<StructType>::Get(), [&InFunction](FStructView InStructView)
		{
			InFunction(InStructView.Get<StructType>());
		});
	}

	// Access the memory of the shared variable struct directly.
	// @param	InStruct			The variable struct type to access
	// @param	InFunction			Function called with a reference to the variable's struct 
	// @return true if the variables could be accessed. Variables can exist but be unable to be accessed if user overrides are set
	bool AccessVariablesStruct(const UScriptStruct* InStruct, TFunctionRef<void(FStructView)> InFunction) const
	{
		return Variables.AccessVariablesStructInternal(InStruct, [&InFunction](FStructView InStructView)
		{
			InFunction(InStructView);
		});
	}

	// Access the memory of the shared variable struct directly.
	// @param	InFunction			Function called with a reference to the variable's struct 
	// @return true if the variables could be accessed. Variables can exist but be unable to be accessed if user overrides are set
	void ForEachVariablesStruct(TFunctionRef<void(FStructView)> InFunction) const
	{
		return Variables.ForEachVariablesStructInternal(InFunction);
	}

	// Find references to all variables of the specified type
	// @param	OutVariables		The variable references we found
	template<typename ValueType>
	void GetAllVariablesOfType(TArray<FAnimNextVariableReference>& OutVariables) const
	{
		return Variables.GetAllVariablesOfType(FAnimNextParamType::GetType<ValueType>(), OutVariables);
	}

	// Returns a typed instance component, creating it lazily the first time it is queried
	template<typename ComponentType>
	ComponentType& GetComponent()
	{
		static_assert(std::is_base_of<FUAFAssetInstanceComponent, ComponentType>::value, "ComponentType type must derive from FUAFAssetInstanceComponent");

		if constexpr (std::is_same_v<ComponentType, FUAFRigVMComponent>)
		{
			if (CachedRigVMComponent != nullptr)
			{
				return *CachedRigVMComponent;
			}
		}

		const UScriptStruct* ComponentStruct = TBaseStructure<ComponentType>::Get();
		const int32 ComponentStructHash = GetTypeHash(ComponentStruct);

		if (TInstancedStruct<FUAFAssetInstanceComponent>* InstanceComponent = ComponentMap.FindByHash(ComponentStructHash, ComponentStruct))
		{
			return InstanceComponent->GetMutable<ComponentType>();
		}

		FUAFAssetInstanceComponent::FScopedConstructorHelper ConstructorHelper(*this);

		TInstancedStruct<ComponentType> NewInstancedStruct(ComponentStruct);
		TInstancedStruct<FUAFAssetInstanceComponent>& InstancedStruct = ComponentMap.AddByHash(ComponentStructHash, ComponentStruct, MoveTemp(NewInstancedStruct));

		if constexpr (std::is_same_v<ComponentType, FUAFRigVMComponent>)
		{
			CachedRigVMComponent = &InstancedStruct.GetMutable<ComponentType>();
			return *CachedRigVMComponent;
		}
		else
		{
			return InstancedStruct.GetMutable<ComponentType>();
		}
	}

	// Returns a typed instance component pointer if found or nullptr otherwise
	template<typename ComponentType>
	ComponentType* TryGetComponent()
	{
		static_assert(std::is_base_of<FUAFAssetInstanceComponent, ComponentType>::value, "ComponentType type must derive from FUAFAssetInstanceComponent");
		
		const UScriptStruct* ComponentStruct = TBaseStructure<ComponentType>::Get();
		const int32 ComponentStructHash = GetTypeHash(ComponentStruct);

		if (TInstancedStruct<FUAFAssetInstanceComponent>* InstanceComponent = ComponentMap.FindByHash(ComponentStructHash, ComponentStruct))
		{
			return InstanceComponent->GetMutablePtr<ComponentType>();
		}
		return nullptr;
	}

	// Returns a typed instance component pointer if found or nullptr otherwise
	UE_API FUAFAssetInstanceComponent* TryGetComponent(const UScriptStruct* InStruct);

	// Returns a typed instance component pointer if found or nullptr otherwise
	template<typename ComponentType>
	const ComponentType* TryGetComponent() const
	{
		static_assert(std::is_base_of<FUAFAssetInstanceComponent, ComponentType>::value, "ComponentType type must derive from FUAFAssetInstanceComponent");
		
		const UScriptStruct* ComponentStruct = TBaseStructure<ComponentType>::Get();
		const int32 ComponentStructHash = GetTypeHash(ComponentStruct);

		if (const TInstancedStruct<FUAFAssetInstanceComponent>* InstanceComponent = ComponentMap.FindByHash(ComponentStructHash, ComponentStruct))
		{
			return InstanceComponent->GetPtr<ComponentType>();
		}
		return nullptr;
	}

	// Iterates all components, calling the supplied predicate. If the predicate returns false, iteration stops.
	template<typename ComponentType>
	void ForEachComponent(TFunctionRef<bool(ComponentType&)> InFunction)
	{
		static_assert(std::is_base_of<FUAFAssetInstanceComponent, ComponentType>::value, "ComponentType type must derive from FUAFAssetInstanceComponent");

		for (TPair<TObjectPtr<const UScriptStruct>, TInstancedStruct<FUAFAssetInstanceComponent>>& Pair : ComponentMap)
		{
			if (ComponentType* ComponentPtr = Pair.Value.GetMutablePtr<ComponentType>())
			{
				if (!InFunction(*ComponentPtr))
				{
					return;
				}
			}
		}
	}

	// Get the instance (graph, module etc.) that owns/hosts us
	FUAFAssetInstance* GetHost() const
	{
		return HostInstance;
	}

	uint64 GetUniqueId() const
	{
#if ANIMNEXT_TRACE_ENABLED
		return UniqueId;
#else
		return 0;
#endif 
	}

#if DO_CHECK
	// Validate that the supplied property bag is of the same layout as this instance's variables
	UE_API bool LayoutMatches(const FInstancedPropertyBag& InPropertyBag) const;
#endif

protected:
	// Used during initialization. Creates variables or references those of the outer host and applies any overrides
	UE_API void InitializeVariables(const TSharedPtr<const UE::UAF::FVariableOverridesCollection>& InOverrides = nullptr);

#if WITH_EDITOR
	// Used during compilation, migrate all variables to new bags according to new defaults. Reapplies any cached overrides.
	UE_API void MigrateVariables();
#endif

	// Releases any components this instance hosts
	UE_API void ReleaseComponents();

	// Access the variable by index directly.
	void AccessVariablePropertyByIndex(int32 InIndex, TFunctionRef<void(const FProperty*, TArrayView<uint8>)> InFunction)
	{
		return Variables.AccessVariablePropertyByIndex(InIndex, InFunction);
	}

protected:
	// Map of components
	UPROPERTY(Transient)
	TMap<TObjectPtr<const UScriptStruct>, TInstancedStruct<FUAFAssetInstanceComponent>> ComponentMap;

	// Container for all variables and overrides
	UPROPERTY(Transient)
	FUAFInstanceVariableData Variables;

	// Hard reference to the asset used to create this instance to ensure we can release it safely
	UPROPERTY(Transient)
	TObjectPtr<const UObject> Asset;

	// The instance (graph, module etc.) that owns/hosts us
	FUAFAssetInstance* HostInstance = nullptr;

	// Cached RigVM component, used to avoid map lookup each time we execute 
	FUAFRigVMComponent* CachedRigVMComponent = nullptr;

	friend UE::UAF::FInjectionInfo;
	friend FAnimNextModuleInjectionComponent;
	friend FUAFInstanceVariableData;
	friend UE::UAF::FAnimNextTrace;
	friend UE::UAF::FInstanceTaskContext;
	friend FUAFRigVMComponent;

#if ANIMNEXT_TRACE_ENABLED
	uint64 UniqueId;
	UE_API volatile static int64 NextUniqueId;
#endif 
};

#undef UE_API