// Copyright Epic Games, Inc. All Rights Reserved.

#include "TemporaryPositionOverride.h"

#include "FlyoutOverlayManager.h"

namespace UE::ControlRigEditor
{
FFlyoutTemporaryPositionOverride::FFlyoutTemporaryPositionOverride(FPrivateToken, FFlyoutOverlayManager& InManager)
	: ManagerToRestore(&InManager)
	{}
	
FFlyoutTemporaryPositionOverride::FFlyoutTemporaryPositionOverride(FFlyoutTemporaryPositionOverride&& Other)
	: ManagerToRestore(Other.ManagerToRestore)
{
	Other.Cancel();
}

FFlyoutTemporaryPositionOverride::~FFlyoutTemporaryPositionOverride()
{
	if (ManagerToRestore)
	{
		ManagerToRestore->StopTemporaryWidgetPosition();
	}
}
}
