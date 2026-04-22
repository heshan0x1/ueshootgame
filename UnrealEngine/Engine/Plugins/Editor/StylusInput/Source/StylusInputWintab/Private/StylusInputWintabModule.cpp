// Copyright Epic Games, Inc. All Rights Reserved.

#include <StylusInputUtils.h>
#include <Modules/ModuleManager.h>

#include "WintabAPI.h"
#include "WintabInterface.h"

namespace UE::StylusInput::Wintab
{
	class FStylusInputWintabModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;

	private:
		TUniquePtr<IStylusInputInterface> RegisteredInterface;
	};

	void FStylusInputWintabModule::StartupModule()
	{
		if (FWintabAPI::DriverIsAvailable())
		{
			RegisteredInterface = FWintabInterface::Create();
			if (RegisteredInterface)
			{
				RegisterInterface(RegisteredInterface.Get());
			}
			else
			{
				UE_LOG(Private::LogStylusInput, Warning, TEXT("Wintab interface initialization failed."));
			}
		}
		else
		{
			UE_LOG(Private::LogStylusInput, Log, TEXT("Wintab driver is not available; Wintab interface will be not be initialized."));
		}
	}

	void FStylusInputWintabModule::ShutdownModule()
	{
		if (RegisteredInterface)
		{
			UnregisterInterface(RegisteredInterface.Get());

			RegisteredInterface.Reset();
		}
	}
}

IMPLEMENT_MODULE(UE::StylusInput::Wintab::FStylusInputWintabModule, StylusInputWintab);

/*
 * TODO
 * [ ] Make Wintab the default (with additional editor settings?)
 * [ ] Propagate buttons data for both Wintab and Windows (PK_BUTTONS and/or StylusButtonDown/StylusButtonDown events)
 * [ ] Queue size
 * [ ] Query multiple packets at once
 * [ ] Async
 * [ ] Deal with cursors not having a physical ID
 * [ ] Allegedly, some Wintab libraries don't handle relative button presses, so maybe switch to absolute?
 */
