// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMAssetViewMenuContext.generated.h"

namespace UE::UAF::Editor
{
	class SRigVMAssetView;
}

UCLASS()
class URigVMAssetViewMenuContext : public UObject
{
	GENERATED_BODY()

	friend class UE::UAF::Editor::SRigVMAssetView;
	
	// The RigVM asset view that we are editing
	TWeakPtr<UE::UAF::Editor::SRigVMAssetView> RigVMAssetView;
};