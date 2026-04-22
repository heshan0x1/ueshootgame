// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

namespace UE::Editor::DataStorage
{
	class FTedsActorCompatibilityModule
		: public IModuleInterface
	{
	public:

		FTedsActorCompatibilityModule() = default;

		// IModuleInterface interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
	};
} // namespace UE::Editor::DataStorage
