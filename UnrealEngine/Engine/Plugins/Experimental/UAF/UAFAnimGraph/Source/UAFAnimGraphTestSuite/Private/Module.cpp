// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

namespace UE::UAF::AnimGraph::Tests
{
	class FModule : public IModuleInterface
	{
	};
}

IMPLEMENT_MODULE(UE::UAF::AnimGraph::Tests::FModule, UAFAnimGraphTestSuite)
