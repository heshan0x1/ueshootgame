// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Styling/SlateStyle.h"

class FPIEPreviewWindowCoreStyle
{

public:

	static PIEPREVIEWDEVICEPROFILESELECTOR_API TSharedRef<class ISlateStyle> Create(const FName& InStyleSetName = "FPIECoreStyle");
	
	static PIEPREVIEWDEVICEPROFILESELECTOR_API void InitializePIECoreStyle();
	
	/** @return the singleton instance. */
	static const ISlateStyle& Get()
	{
		return *(Instance.Get());
	}

private:
		
	static PIEPREVIEWDEVICEPROFILESELECTOR_API TSharedPtr< class ISlateStyle > Instance;
};
#endif
