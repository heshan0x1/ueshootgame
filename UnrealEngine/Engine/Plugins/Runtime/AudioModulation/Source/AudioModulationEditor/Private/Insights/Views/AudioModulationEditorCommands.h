// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"

class FUICommandInfo;

namespace AudioModulationEditor
{
	class FAudioModulationEditorCommands : public TCommands<FAudioModulationEditorCommands>
	{
	public:
		FAudioModulationEditorCommands();

		virtual void RegisterCommands() override;

		TSharedPtr<const FUICommandInfo> GetBrowseCommand() const { return Browse; }
		TSharedPtr<const FUICommandInfo> GetEditCommand() const   { return Edit; }
		
	private:
		TSharedPtr<FUICommandInfo> Browse;
		TSharedPtr<FUICommandInfo> Edit;
	};
} // namespace UE::Audio::Insights
