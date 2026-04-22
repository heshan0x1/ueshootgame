// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"

class FUICommandInfo;

namespace UE::Audio::Insights
{
	class FAudioEventLogEditorCommands : public TCommands<FAudioEventLogEditorCommands>
	{
	public:
		FAudioEventLogEditorCommands();

		virtual void RegisterCommands() override;

		TSharedPtr<const FUICommandInfo> GetBrowseCommand() const { return Browse; }
		TSharedPtr<const FUICommandInfo> GetEditCommand() const { return Edit; }
		TSharedPtr<const FUICommandInfo> GetResetInspectTimestampCommand() const { return ResetInspectTimestampEventLog; }

		TSharedPtr<const FUICommandInfo> GetAutoStopCachingWhenLastInCacheCommand() const { return AutoStopCachingWhenLastInCache; }
		TSharedPtr<const FUICommandInfo> GetAutoStopCachingOnInspectCommand() const { return AutoStopCachingOnInspect; }
		TSharedPtr<const FUICommandInfo> GetAutoStopCachingDisabledCommand() const { return AutoStopCachingDisabled; }

	private:
		TSharedPtr<FUICommandInfo> Browse;
		TSharedPtr<FUICommandInfo> Edit;
		TSharedPtr<FUICommandInfo> ResetInspectTimestampEventLog;

		TSharedPtr<FUICommandInfo> AutoStopCachingWhenLastInCache;
		TSharedPtr<FUICommandInfo> AutoStopCachingOnInspect;
		TSharedPtr<FUICommandInfo> AutoStopCachingDisabled;
	};
} // namespace UE::Audio::Insights
