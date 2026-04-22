// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

struct FToolMenuEntry;

namespace Chaos::VisualDebugger::Menus
{

const FName ShowMenuName = "ChaosVDViewportToolbarBase.Show";

FToolMenuEntry CreateShowSubmenu();

FToolMenuEntry CreateSettingsSubmenu();

}
