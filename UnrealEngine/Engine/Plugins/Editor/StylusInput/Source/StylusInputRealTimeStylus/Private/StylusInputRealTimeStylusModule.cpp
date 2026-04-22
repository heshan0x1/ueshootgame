// Copyright Epic Games, Inc. All Rights Reserved.

#include <Modules/ModuleManager.h>

#include "RealTimeStylusInterface.h"

namespace UE::StylusInput::RealTimeStylus
{
	class FStylusInputRealTimeStylusModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;

	private:
		TUniquePtr<IStylusInputInterface> RegisteredInterface;
	};

	void FStylusInputRealTimeStylusModule::StartupModule()
	{
		RegisteredInterface = FRealTimeStylusInterface::Create();
		if (RegisteredInterface)
		{
			RegisterInterface(RegisteredInterface.Get());
		}
	}

	void FStylusInputRealTimeStylusModule::ShutdownModule()
	{
		if (RegisteredInterface)
		{
			UnregisterInterface(RegisteredInterface.Get());

			RegisteredInterface.Reset();
		}
	}
}

IMPLEMENT_MODULE(UE::StylusInput::RealTimeStylus::FStylusInputRealTimeStylusModule, StylusInputRealTimeStylus);

/*
 * TODO
 * [ ] Propagate buttons data for StylusButtonDown/StylusButtonUp
 */
