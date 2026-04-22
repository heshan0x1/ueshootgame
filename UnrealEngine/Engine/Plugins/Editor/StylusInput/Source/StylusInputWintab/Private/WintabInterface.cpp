// Copyright Epic Games, Inc. All Rights Reserved.

#include "WintabInterface.h"

#include <StylusInput.h>
#include <StylusInputUtils.h>

#include <GenericPlatform/GenericWindow.h>
#include <Templates/SharedPointer.h>
#include <Widgets/SWindow.h>

#include "WintabInstance.h"
#include "WintabAPI.h"

#define LOCTEXT_NAMESPACE "WintabInterface"

using namespace UE::StylusInput::Private;

namespace UE::StylusInput::Wintab
{
	FName FWintabInterface::GetName() const
	{
		return FWintabAPI::GetName();
	}

	IStylusInputInstance* FWintabInterface::CreateInstance(SWindow& Window)
	{
		if (FRefCountedInstance* ExistingRefCountedInstance = Instances.Find(&Window))
		{
			++ExistingRefCountedInstance->RefCount;
			return ExistingRefCountedInstance->Instance.Get();
		}

		HWND OSWindowHandle = [&Window]
		{
			const TSharedPtr<const FGenericWindow> NativeWindow = Window.GetNativeWindow();
			return NativeWindow.IsValid() ? static_cast<HWND>(NativeWindow->GetOSWindowHandle()) : nullptr;
		}();

		if (!OSWindowHandle)
		{
			LogError("WintabInterface", "Could not get native window handle.");
			return nullptr;
		}

		FWintabInstance* NewInstance = Instances.Emplace(
			&Window, {MakeUnique<FWintabInstance>(NextInstanceID++, OSWindowHandle), 1}).Instance.Get();
		if (!ensureMsgf(NewInstance, TEXT("WintabInterface: Failed to create stylus input instance.")))
		{
			Instances.Remove(&Window);
			return nullptr;
		}

		return NewInstance;
	}

	bool FWintabInterface::ReleaseInstance(IStylusInputInstance* Instance)
	{
		check(Instance);

		FWintabInstance *const WindowsInstance = static_cast<FWintabInstance*>(Instance);

		// Find existing instance
		for (TTuple<SWindow*, FRefCountedInstance>& Entry : Instances)
		{
			FRefCountedInstance& RefCountedInstance = Entry.Get<1>();

			if (RefCountedInstance.Instance.Get() == WindowsInstance)
			{
				// Decrease reference count
				check(RefCountedInstance.RefCount > 0);
				if (--RefCountedInstance.RefCount == 0)
				{
					// Delete if there are no references left
					Instances.Remove(Entry.Key);
				}

				return true;
			}
		}

		ensureMsgf(false, TEXT("WintabInterface: Failed to find provided instance."));
		return false;
	}

	TUniquePtr<IStylusInputInterface> FWintabInterface::Create()
	{
		if (FWintabAPI::GetInstance().IsValid())
		{
			TUniquePtr<FWintabInterface> WintabInterface = MakeUnique<FWintabInterface>();
			return TUniquePtr<IStylusInputInterface>(MoveTemp(WintabInterface));
		}

		return nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
