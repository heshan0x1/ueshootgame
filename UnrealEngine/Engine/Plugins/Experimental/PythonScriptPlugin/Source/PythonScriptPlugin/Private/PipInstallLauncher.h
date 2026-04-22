// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPipInstall.h"

namespace PipInstallLauncher
{
	bool StartSync(IPipInstall& PipInstall, FSimpleDelegate OnCompleted = FSimpleDelegate());
	bool StartAsync(IPipInstall& PipInstall, FSimpleDelegate OnCompleted = FSimpleDelegate());
}
