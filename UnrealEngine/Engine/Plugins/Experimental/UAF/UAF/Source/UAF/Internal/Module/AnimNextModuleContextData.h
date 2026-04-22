// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UAFAssetInstance.h"
#include "AnimNextModuleContextData.generated.h"

struct FAnimNextModuleInstance;

USTRUCT()
struct FAnimNextModuleContextData
{
	GENERATED_BODY()

	FAnimNextModuleContextData() = default;

	UAF_API explicit FAnimNextModuleContextData(FAnimNextModuleInstance* InModuleInstance);

	UAF_API FAnimNextModuleContextData(FAnimNextModuleInstance* InModuleInstance, const FUAFAssetInstance* InInstance);

	// Get the object that the module instance is bound to, if any
	UAF_API UObject* GetObject() const;

	FAnimNextModuleInstance& GetModuleInstance() const
	{
		check(ModuleInstance != nullptr);
		return *ModuleInstance;
	}

	const FUAFAssetInstance& GetInstance() const
	{
		check(Instance != nullptr);
		return *Instance;
	}

private:
	// Module instance that is currently executing.
	FAnimNextModuleInstance* ModuleInstance = nullptr;

	// Instance that is currently executing. Can be the same as ModuleInstance
	const FUAFAssetInstance* Instance = nullptr;

	friend class UAnimNextModule;
	friend struct FAnimNextExecuteContext;
};
