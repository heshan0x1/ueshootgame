// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UAFAssetInstanceComponent.generated.h"

struct FUAFAssetInstance;
struct FAnimNextGraphInstance;
struct FAnimNextModuleInstance;

#define UE_API UAF_API

/** An asset instance component is attached and owned by an asset instance. */
USTRUCT()
struct FUAFAssetInstanceComponent
{
	GENERATED_BODY()

	using ContainerType = FUAFAssetInstance;

	UE_API FUAFAssetInstanceComponent();

	virtual ~FUAFAssetInstanceComponent() = default;

	// Returns the owning asset instance this component lives on
	FUAFAssetInstance* GetAssetInstancePtr()
	{
		return Instance;
	}

	// Returns the owning asset instance this component lives on
	FUAFAssetInstance& GetAssetInstance()
	{
		check(Instance != nullptr);
		return *Instance;
	}

	// Returns the owning asset instance this component lives on
	const FUAFAssetInstance& GetAssetInstance() const
	{
		check(Instance != nullptr);
		return *Instance;
	}

private:
	// Helper struct for supplying asset instance references to constructor via TLS
	struct FScopedConstructorHelper
	{
		UE_API FScopedConstructorHelper(FUAFAssetInstance& InInstance);
		UE_API ~FScopedConstructorHelper();
	};
	
protected:
	// The owning asset instance this component lives on
	FUAFAssetInstance* Instance = nullptr;

	friend FUAFAssetInstance;
	friend FAnimNextGraphInstance;
	friend FAnimNextModuleInstance;
};

#undef UE_API