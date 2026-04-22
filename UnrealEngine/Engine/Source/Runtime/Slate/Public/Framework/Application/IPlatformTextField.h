// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"
#include "Features/IModularFeature.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"

#define UE_API SLATE_API


class IVirtualKeyboardEntry;

class IPlatformTextField
{
public:
	virtual ~IPlatformTextField() {};

	virtual void ShowVirtualKeyboard(bool bShow, int32 UserIndex, TSharedPtr<IVirtualKeyboardEntry> TextEntryWidget) = 0;
	virtual bool AllowMoveCursor() { return true; }

	static bool ShouldUseVirtualKeyboardAutocorrect(TSharedPtr<IVirtualKeyboardEntry> TextEntryWidget);

private:

};


class IPlatformTextFieldFactory : public IModularFeature
{
public:
	UE_API static FName FeatureName;

	UE_API static TUniquePtr<IPlatformTextField> TryCreateInstance();
	
	virtual TUniquePtr<IPlatformTextField> CreateInstance() = 0;


};

#undef UE_API
