// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FPixelStreaming2ServersModule : public IModuleInterface
{
public:
	virtual ~FPixelStreaming2ServersModule() = default;
	virtual void StartupModule() override;

public:
	static FPixelStreaming2ServersModule& Get();

};