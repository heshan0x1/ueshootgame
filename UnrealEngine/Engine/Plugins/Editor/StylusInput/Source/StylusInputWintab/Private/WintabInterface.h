// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <StylusInput.h>
#include <StylusInputInterface.h>
#include <Containers/Map.h>
#include <Templates/UniquePtr.h>

namespace UE::StylusInput::Wintab
{
	class FWintabInstance;

	class FWintabInterface : public IStylusInputInterface
	{
	public:
		virtual FName GetName() const override;

		virtual IStylusInputInstance* CreateInstance(SWindow& Window) override;
		virtual bool ReleaseInstance(IStylusInputInstance* Instance) override;

	private:
		friend class FStylusInputWintabModule;

		static TUniquePtr<IStylusInputInterface> Create();

		struct FRefCountedInstance
		{
			TUniquePtr<FWintabInstance> Instance;
			int32 RefCount;
		};

		TMap<SWindow*, FRefCountedInstance> Instances;
		uint32 NextInstanceID = 0;
	};
}
