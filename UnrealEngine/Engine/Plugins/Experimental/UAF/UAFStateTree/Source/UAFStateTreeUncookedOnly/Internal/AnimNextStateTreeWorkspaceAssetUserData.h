// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"

#include "AnimNextAnimGraphWorkspaceAssetUserData.h"
#include "AnimNextStateTreeWorkspaceAssetUserData.generated.h"

UCLASS()
class UAnimNextStateTreeWorkspaceAssetUserData : public UAnimNextAnimGraphWorkspaceAssetUserData
{
	GENERATED_BODY()

protected:

	//~ Begin UAnimNextAnimGraphWorkspaceAssetUserData Interface
	virtual void GetRootAssetExport(FAssetRegistryTagsContext Context) const override;
	virtual void GetWorkspaceAssetExports(FAssetRegistryTagsContext Context) const override;
	//~ End UAnimNextAnimGraphWorkspaceAssetUserData Interface
};
