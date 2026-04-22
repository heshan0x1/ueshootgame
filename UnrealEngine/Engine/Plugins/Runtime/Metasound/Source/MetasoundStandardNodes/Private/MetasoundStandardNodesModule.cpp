// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "MetasoundFrontendModuleRegistrationMacros.h"
#include "MetasoundFrontendNodeClassRegistry.h"
#include "MetasoundFrontendNodeMigration.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


namespace Metasound 
{
	class FMetasoundStandardNodesModule : public IModuleInterface
	{
		virtual void StartupModule() override
		{
			using namespace Frontend;
			METASOUND_REGISTER_ITEMS_IN_MODULE

#if 0
			// Example of node migration
			INodeClassRegistry& NodeRegistry = INodeClassRegistry::GetChecked();
			NodeRegistry.RegisterNodeMigration(
				FNodeMigrationInfo
				{
					"5.7",
					FNodeClassName {"Dummy", "Dummy", "Dummy"},
					1,
					0,
					UE_STRINGIZE(METASOUND_PLUGIN),
					UE_STRINGIZE(METASOUND_MODULE),
					"MetasoundExperimental",
					"MetasoundRuntime"
				});
#endif
		}

		virtual void ShutdownModule() override
		{
			using namespace Frontend;
			METASOUND_UNREGISTER_ITEMS_IN_MODULE
			

#if 0
			// Example of node migration
			INodeClassRegistry& NodeRegistry = INodeClassRegistry::GetChecked();
			NodeRegistry.UnregisterNodeMigration(
				FNodeMigrationInfo
				{
					"5.7",
					FNodeClassName {"Dummy", "Dummy", "Dummy"},
					1,
					0,
					UE_STRINGIZE(METASOUND_PLUGIN),
					UE_STRINGIZE(METASOUND_MODULE),
					"MetasoundExperimental",
					"MetasoundRuntime"
				});
#endif
		}
	};
}

METASOUND_IMPLEMENT_MODULE_REGISTRATION_LIST
IMPLEMENT_MODULE(Metasound::FMetasoundStandardNodesModule, MetasoundStandardNodes);

