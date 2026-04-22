// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanFootageIngest is deprecated. This functionality is now available in the CaptureManager module")
	FCaptureManagerCommands : public TCommands<FCaptureManagerCommands>
{
public:

	FCaptureManagerCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> Save;
	TSharedPtr<FUICommandInfo> SaveAll;
	TSharedPtr<FUICommandInfo> Refresh;

	TSharedPtr<FUICommandInfo> StartStopCapture;
};