// Copyright Epic Games, Inc. All Rights Reserved.
#include "Messages/SubmixTraceMessages.h"

namespace UE::Audio::Insights
{
	namespace SubmixMessageNames
	{
		const FName Loaded = "SubmixLoaded";
		const FName IsAlivePing = "SubmixAlivePing";
		const FName HasActivity = "SubmixHasActivity";
		const FName EnvelopeFollowerEnabled = "SubmixEnvelopeFollowerEnabled";
		const FName EnvelopeValues = "SubmixEnvelopeValues";
		const FName Unloaded = "SubmixUnloaded";
	}

	// FSubmixMessageBase
	FSubmixMessageBase::FSubmixMessageBase(const Trace::IAnalyzer::FOnEventContext& InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
		SubmixId = EventData.GetValue<uint32>("SubmixId");
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
	}

	// FSubmixLoadedMessage
	FSubmixLoadedMessage::FSubmixLoadedMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		EventData.GetString("Name", Name);
		NumChannels = EventData.GetValue<int32>("NumChannels");
		bIsMainSubmix = EventData.GetValue<bool>("IsMainSubmix");
	}

	uint32 FSubmixLoadedMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FSubmixLoadedMessage);

		// Add any dynamically sized members
		MemorySize += Name.GetAllocatedSize();

		return MemorySize;
	};

	// FSubmixAlivePingMessage
	FSubmixAlivePingMessage::FSubmixAlivePingMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		EventData.GetString("Name", Name);
		NumChannels = EventData.GetValue<int32>("NumChannels");
		bIsMainSubmix = EventData.GetValue<bool>("IsMainSubmix");
	}

	uint32 FSubmixAlivePingMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FSubmixAlivePingMessage);

		// Add any dynamically sized members
		MemorySize += Name.GetAllocatedSize();

		return MemorySize;
	};

	// FSubmixHasActivityMessage
	FSubmixHasActivityMessage::FSubmixHasActivityMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		bHasActivity = EventData.GetValue<bool>("HasActivity");
	}

	// FSubmixEnvelopeFollowerEnabledMessage
	FSubmixEnvelopeFollowerEnabledMessage::FSubmixEnvelopeFollowerEnabledMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		bEnvelopeFollowerEnabled = EventData.GetValue<bool>("EnvelopeFollowerEnabled");
	}

	// FSubmixEnvelopeValuesMessage
	FSubmixEnvelopeValuesMessage::FSubmixEnvelopeValuesMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		const TArrayView<const float> EnvelopeValuesView = EventData.GetArrayView<float>("EnvelopeValues");

		for (const auto& EnvelopeValue : EnvelopeValuesView)
		{
			EnvelopeValues.Emplace(EnvelopeValue);
		}
	}

	uint32 FSubmixEnvelopeValuesMessage::GetSizeOf() const
	{
		// Start with the fixed size of the struct
		uint32 MemorySize = sizeof(FSubmixEnvelopeValuesMessage);

		// Add any dynamically sized members
		if (EnvelopeValues.Num() > 0)
		{
			MemorySize += EnvelopeValues.Num() * sizeof(EnvelopeValues[0]);
		}

		return MemorySize;
	};

	// FSubmixUnloadedMessage
	FSubmixUnloadedMessage::FSubmixUnloadedMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FSubmixMessageBase(InContext)
	{

	};

	// FSubmixDashboardEntry
	FSubmixDashboardEntry::FSubmixDashboardEntry()
		: AudioMeterInfo(MakeShared<FAudioMeterInfo>())
	{
	}

	FSubmixDashboardEntry::FSubmixDashboardEntry(FSubmixDashboardEntry& Other)
		: SubmixId(Other.SubmixId)
		, Timestamp(Other.Timestamp)
		, AudioMeterInfo(MakeShared<FAudioMeterInfo>(*Other.AudioMeterInfo))
		, Name(Other.Name)
		, bHasActivity(Other.bHasActivity)
		, bEnvelopeFollowerEnabled(Other.bEnvelopeFollowerEnabled)
		, bIsMainSubmix(Other.bIsMainSubmix)
	{
	}

	FSubmixDashboardEntry& FSubmixDashboardEntry::operator=(const FSubmixDashboardEntry& Other)
	{
		if (this == &Other)
		{
			return *this;
		}

		SubmixId = Other.SubmixId;
		Timestamp = Other.Timestamp;
		AudioMeterInfo = MakeShared<FAudioMeterInfo>(*Other.AudioMeterInfo);
		Name = Other.Name;
		bHasActivity = Other.bHasActivity;
		bEnvelopeFollowerEnabled = Other.bEnvelopeFollowerEnabled;
		bIsMainSubmix = Other.bIsMainSubmix;

		return *this;
	}
} // namespace UE::Audio::Insights
