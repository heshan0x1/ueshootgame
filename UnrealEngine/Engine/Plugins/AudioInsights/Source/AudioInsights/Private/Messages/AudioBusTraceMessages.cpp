// Copyright Epic Games, Inc. All Rights Reserved.
#include "Messages/AudioBusTraceMessages.h"

namespace UE::Audio::Insights
{
	namespace AudioBusMessageNames
	{
		const FName Start = "AudioBusStart";
		const FName HasActivity = "AudioBusHasActivity";
		const FName EnvelopeFollowerEnabled = "AudioBusEnvelopeFollowerEnabled";
		const FName EnvelopeValues = "AudioBusEnvelopeValues";
		const FName Stop = "AudioBusStop";
	};

	// FAudioBusMessageBase
	FAudioBusMessageBase::FAudioBusMessageBase(const Trace::IAnalyzer::FOnEventContext& InContext)
    {
		const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
		AudioBusId = EventData.GetValue<uint32>("AudioBusId");
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
    }

	// FAudioBusStartMessage
	FAudioBusStartMessage::FAudioBusStartMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FAudioBusMessageBase(InContext)
	{
		const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		EventData.GetString("Name", Name);
		NumChannels = EventData.GetValue<int32>("NumChannels");

		AudioBusType = FSoftObjectPath(Name).IsAsset() ? EAudioBusType::AssetBased : EAudioBusType::CodeGenerated;
	}

	uint32 FAudioBusStartMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FAudioBusStartMessage);

		// Add any dynamically sized members
		MemorySize += Name.GetAllocatedSize();

		return MemorySize;
	}

	// FAudioBusHasActivityMessage
	FAudioBusHasActivityMessage::FAudioBusHasActivityMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FAudioBusMessageBase(InContext)
	{
		const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		bHasActivity = EventData.GetValue<bool>("HasActivity");
	}

	// FAudioBusEnvelopeFollowerEnabledMessage
	FAudioBusEnvelopeFollowerEnabledMessage::FAudioBusEnvelopeFollowerEnabledMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FAudioBusMessageBase(InContext)
	{
		const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		bEnvelopeFollowerEnabled = EventData.GetValue<bool>("EnvelopeFollowerEnabled");
	}

	// FAudioBusEnvelopeValuesMessage
	FAudioBusEnvelopeValuesMessage::FAudioBusEnvelopeValuesMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FAudioBusMessageBase(InContext)
	{
		const UE::Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		const TArrayView<const float> EnvelopeValuesView = EventData.GetArrayView<float>("EnvelopeValues");

		for (const auto& EnvelopeValue : EnvelopeValuesView)
		{
			EnvelopeValues.Emplace(EnvelopeValue);
		}
	}

	uint32 FAudioBusEnvelopeValuesMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FAudioBusEnvelopeValuesMessage);

		// Add any dynamically sized members
		if (EnvelopeValues.Num() > 0)
		{
			MemorySize += EnvelopeValues.Num() * sizeof(EnvelopeValues[0]);
		}

		return MemorySize;
	}

	// FAudioBusStopMessage
	FAudioBusStopMessage::FAudioBusStopMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FAudioBusMessageBase(InContext)
	{
	}

	// FAudioBusDashboardEntry
	FAudioBusDashboardEntry::FAudioBusDashboardEntry()
		: AudioMeterInfo(MakeShared<FAudioMeterInfo>())
	{
	}

	FAudioBusDashboardEntry::FAudioBusDashboardEntry(FAudioBusDashboardEntry& Other)
		: AudioBusId(Other.AudioBusId)
		, Timestamp(Other.Timestamp)
		, AudioMeterInfo(MakeShared<FAudioMeterInfo>(*Other.AudioMeterInfo))
		, Name(Other.Name)
		, bHasActivity(Other.bHasActivity)
		, bEnvelopeFollowerEnabled(Other.bEnvelopeFollowerEnabled)
		, AudioBusType(Other.AudioBusType)
	{
	}

	FAudioBusDashboardEntry& FAudioBusDashboardEntry::operator=(const FAudioBusDashboardEntry& Other)
	{
		if (this == &Other)
		{
			return *this;
		}

		AudioBusId = Other.AudioBusId;
		Timestamp = Other.Timestamp;
		AudioMeterInfo = MakeShared<FAudioMeterInfo>(*Other.AudioMeterInfo);
		Name = Other.Name;
		bHasActivity = Other.bHasActivity;
		bEnvelopeFollowerEnabled = Other.bEnvelopeFollowerEnabled;
		AudioBusType = Other.AudioBusType;

		return *this;
	}
} // namespace UE::Audio::Insights
