// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableOverrides.h"

#include "AnimNextRigVMAsset.h"

namespace UE::UAF
{

FVariableOverrides::FVariableOverrides(const UAnimNextRigVMAsset* InAsset, TArray<FOverride>&& InOverrides)
	: Overrides(MoveTemp(InOverrides))
{
	check(InAsset);

	AssetOrStructData.Set<FAssetType>(InAsset);
}

FVariableOverrides::FVariableOverrides(const UScriptStruct* InStruct, TArray<FOverride>&& InOverrides)
	: Overrides(MoveTemp(InOverrides))
{
	AssetOrStructData.Set<FStructType>(InStruct);

}

}
