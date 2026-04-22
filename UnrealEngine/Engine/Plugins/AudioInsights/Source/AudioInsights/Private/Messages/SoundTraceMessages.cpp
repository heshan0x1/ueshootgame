// Copyright Epic Games, Inc. All Rights Reserved.
#include "Messages/SoundTraceMessages.h"

namespace UE::Audio::Insights
{
	namespace SoundMessageNames
	{
		const FName SoundStart = "SoundStart";
		const FName SoundIsAlivePing = "SoundIsAlivePing";
		const FName SoundWaveStart = "SoundWaveStart";
		const FName SoundWaveIsAlivePing = "SoundWaveIsAlivePing";
		const FName SoundStop = "SoundStop";

		const FName PriorityParam = "Priority";
		const FName DistanceParam = "Distance";
		const FName DistanceAttenuationParam = "DistanceAttenuation";
		const FName HPFFreqParam = "HPFFreq";
		const FName LPFFreqParam = "LPFFreq";
		const FName EnvelopeParam = "Envelope";
		const FName PitchParam = "Pitch";
		const FName VolumeParam = "Volume";
		const FName RelativeRenderCostParam = "RelativeRenderCost";
	};

	namespace SoundMessageUtils
	{
		uint64 GeneratePlayOrderUniqueID(const uint32 ActiveSoundPlayOrder, const uint32 WaveInstancePlayOrder)
		{
			return (static_cast<uint64>(ActiveSoundPlayOrder) << 32) + static_cast<uint64>(WaveInstancePlayOrder);
		}
	};

	FSoundMessageBase::FSoundMessageBase(const Trace::IAnalyzer::FOnEventContext& InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
	}

	FSoundStartMessage::FSoundStartMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSoundMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;
		EventData.GetString("AssetPath", Name);
		ActiveSoundPlayOrder = EventData.GetValue<uint32>("PlayOrder");

		FString SoundClassName;

		EventData.GetString("SoundClassName", SoundClassName);

		if (SoundClassName == SoundClassNames::SoundCue)
		{
			EntryType = ESoundDashboardEntryType::SoundCue;
		}
		else if (SoundClassName == SoundClassNames::SoundWave)
		{
			EntryType = ESoundDashboardEntryType::SoundWave;
		}
		else if (SoundClassName == SoundClassNames::MetaSoundSource)
		{
			EntryType = ESoundDashboardEntryType::MetaSound;
		}
		else if (SoundClassName == SoundClassNames::SoundWaveProcedural)
		{
			EntryType = ESoundDashboardEntryType::ProceduralSource;
		}
		else if (SoundClassName == SoundClassNames::SoundCueTemplate)
		{
			EntryType = ESoundDashboardEntryType::SoundCueTemplate;
		}
		else
		{
			EntryType = ESoundDashboardEntryType::None;
		}

		EventData.GetString("ActorLabel", ActorLabel);
	}

	uint32 FSoundStartMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FSoundStartMessage);

		// Add any dynamically sized members
		MemorySize += Name.GetAllocatedSize();
		MemorySize += ActorLabel.GetAllocatedSize();

		return MemorySize;
	}

	uint32 FSoundWaveStartMessage::GetSizeOf() const
	{
		return FSoundStartMessage::GetSizeOf() + sizeof(ActiveSoundPlayOrder);
	}

	uint32 FSoundStopMessage::GetSizeOf() const
	{
		return sizeof(FSoundStopMessage);
	}

	FSoundDashboardEntry::FSoundDashboardEntry()
	{
		constexpr uint32 DataPointsCapacity = 256u;

		PriorityDataRange.SetCapacity(DataPointsCapacity);
		DistanceDataRange.SetCapacity(DataPointsCapacity);
		DistanceAttenuationDataRange.SetCapacity(DataPointsCapacity);
		LPFFreqDataRange.SetCapacity(DataPointsCapacity);
		HPFFreqDataRange.SetCapacity(DataPointsCapacity);
		AmplitudeDataRange.SetCapacity(DataPointsCapacity);
		VolumeDataRange.SetCapacity(DataPointsCapacity);
		PitchDataRange.SetCapacity(DataPointsCapacity);
		RelativeRenderCostDataRange.SetCapacity(DataPointsCapacity);
	}

	void FSoundDashboardEntry::ResetDataBuffers(const uint32 DataPointsCapacity)
	{
		PriorityDataRange.Reset(DataPointsCapacity);
		DistanceDataRange.Reset(DataPointsCapacity);
		DistanceAttenuationDataRange.Reset(DataPointsCapacity);
		LPFFreqDataRange.Reset(DataPointsCapacity);
		HPFFreqDataRange.Reset(DataPointsCapacity);
		AmplitudeDataRange.Reset(DataPointsCapacity);
		VolumeDataRange.Reset(DataPointsCapacity);
		PitchDataRange.Reset(DataPointsCapacity);
		RelativeRenderCostDataRange.Reset(DataPointsCapacity);
	}
}
