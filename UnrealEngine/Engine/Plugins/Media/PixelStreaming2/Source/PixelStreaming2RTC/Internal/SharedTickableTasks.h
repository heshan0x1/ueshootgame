// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcConferenceUtils.h"
#include "TickableTask.h"

namespace UE::PixelStreaming2
{
	class FSharedTickableTasks
	{
	public:
		FSharedTickableTasks(TSharedPtr<FEpicRtcTickConferenceTask> InTickConferenceTask)
			: TickConferenceTask(InTickConferenceTask)
		{
		}

	private:
		
		TSharedPtr<FEpicRtcTickConferenceTask> TickConferenceTask;
	};
} // namespace UE::PixelStreaming2
