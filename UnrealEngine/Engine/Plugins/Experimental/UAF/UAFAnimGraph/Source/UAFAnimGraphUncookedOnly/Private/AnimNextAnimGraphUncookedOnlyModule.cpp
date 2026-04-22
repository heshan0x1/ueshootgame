// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UAFAnimGraphUncookedOnlyStyle.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Graph/RigVMTrait_AnimNextPublicVariablesUncookedOnly.h"
#include "Templates/GraphNodeTemplateRegistry.h"

#define LOCTEXT_NAMESPACE "FAnimNextAnimGraphUncookedOnlyModule"

namespace UE::UAF::AnimGraph::UncookedOnly
{

class FAnimNextAnimGraphUncookedOnlyModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		using namespace UE::UAF::UncookedOnly;

		FPublicVariablesImpl::Register();
		FUAFAnimGraphUncookedOnlyStyle::Get();
	}

	virtual void ShutdownModule() override
	{
		using namespace UE::UAF::UncookedOnly;

		FGraphNodeTemplateRegistry::Shutdown();
	}
};

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::UAF::AnimGraph::UncookedOnly::FAnimNextAnimGraphUncookedOnlyModule, UAFAnimGraphUncookedOnly);
