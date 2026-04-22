// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioParameterInterfaceRegistry.h"
#include "Interfaces/MetasoundTestInterfaces.h"
#include "MetasoundSource.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

namespace Metasound 
{
	class METASOUNDENGINETEST_API FMetasoundEngineTestModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
			FModuleManager::Get().LoadModuleChecked("MetasoundFrontend");
			FModuleManager::Get().LoadModuleChecked("MetasoundStandardNodes");
			FModuleManager::Get().LoadModuleChecked("MetasoundEngine");

			using namespace Metasound::Test;

			Audio::IAudioParameterInterfaceRegistry& Registry = Audio::IAudioParameterInterfaceRegistry::Get();
			Registry.RegisterInterface(UpdateTestInterface_0_1::CreateInterface(*UMetaSoundSource::StaticClass()));
			Registry.RegisterInterface(UpdateTestInterface_0_2::CreateInterface(*UMetaSoundSource::StaticClass()));
		}
	};
}

IMPLEMENT_MODULE(Metasound::FMetasoundEngineTestModule, MetasoundEngineTest);




