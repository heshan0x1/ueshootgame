// Copyright Epic Games, Inc. All Rights Reserved.
#include "Messages/AudioEventLogTraceMessages.h"

namespace UE::Audio::Insights
{
    FAudioEventLogMessage::FAudioEventLogMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
    {
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		static uint32 MessageIDTracker = 0u;
		MessageID = MessageIDTracker++;

		DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
		PlayOrder = EventData.GetValue<uint32>("PlayOrder");

		EventData.GetString("EventLogName", EventName);
		EventData.GetString("AssetPath", AssetPath);
		EventData.GetString("ActorLabel", ActorLabel);
		EventData.GetString("ActorIconName", ActorIconName);
		EventData.GetString("SoundClassName", CategoryName);

		if (CategoryName == SoundClassNames::SoundCue)
		{
			CategoryType = EAudioEventLogSoundCategory::SoundCue;
		}
		else if (CategoryName == SoundClassNames::SoundWave)
		{
			CategoryType = EAudioEventLogSoundCategory::SoundWave;
		}
		else if (CategoryName == SoundClassNames::MetaSoundSource)
		{
			CategoryType = EAudioEventLogSoundCategory::MetaSound;
		}
		else if (CategoryName == SoundClassNames::SoundWaveProcedural)
		{
			CategoryType = EAudioEventLogSoundCategory::ProceduralSource;
		}
		else if (CategoryName == SoundClassNames::SoundCueTemplate)
		{
			CategoryType = EAudioEventLogSoundCategory::SoundCueTemplate;
		}
		else
		{
			CategoryType = EAudioEventLogSoundCategory::None;
		}
    }

	uint32 FAudioEventLogMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FAudioEventLogMessage);

		// Add any dynamically sized members
		MemorySize += EventName.GetAllocatedSize();
		MemorySize += AssetPath.GetAllocatedSize();
		MemorySize += ActorLabel.GetAllocatedSize();
		MemorySize += ActorIconName.GetAllocatedSize();
		MemorySize += CategoryName.GetAllocatedSize();

		return MemorySize;
	}

	const FName FAudioEventLogMessage::GetMessageName() const
	{
		static const FLazyName EventLogName = "EventLog";
		return EventLogName;
	}
}