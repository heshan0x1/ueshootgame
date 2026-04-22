// Copyright Epic Games, Inc. All Rights Reserved.
#include "Messages/VirtualLoopTraceMessages.h"

namespace UE::Audio::Insights
{
	namespace VirtualLoopMessageNames
	{
		extern const FName Virtualize    = "VirtualLoopVirtualize";
		extern const FName StopOrRealize = "VirtualLoopStopOrRealize";
		extern const FName Update        = "VirtualLoopUpdate";
	};

	// FVirtualLoopMessageBase
	FVirtualLoopMessageBase::FVirtualLoopMessageBase(const Trace::IAnalyzer::FOnEventContext& InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		DeviceId = static_cast<::Audio::FDeviceId>(EventData.GetValue<uint32>("DeviceId"));
		Timestamp = InContext.EventTime.AsSeconds(EventData.GetValue<uint64>("Timestamp"));
		PlayOrder = EventData.GetValue<uint32>("PlayOrder");
	}

	// FVirtualLoopVirtualizeMessage
	FVirtualLoopVirtualizeMessage::FVirtualLoopVirtualizeMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FVirtualLoopMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		EventData.GetString("Name", Name);
		ComponentId = EventData.GetValue<uint64>("ComponentId");
	}
	
	uint32 FVirtualLoopVirtualizeMessage::GetSizeOf() const
	{
		// Fixed size of the struct
		uint32 MemorySize = sizeof(FVirtualLoopVirtualizeMessage);

		// Adde dynamically sized members
		MemorySize += Name.GetAllocatedSize();

		return MemorySize;
	}
	
	// FVirtualLoopStopOrRealizeMessage
	FVirtualLoopStopOrRealizeMessage::FVirtualLoopStopOrRealizeMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FVirtualLoopMessageBase(InContext)
	{
	}

	uint32 FVirtualLoopStopOrRealizeMessage::GetSizeOf() const
	{
		// Returning fixed size of the struct (no dynamically sized members in it)
		return sizeof(FVirtualLoopStopOrRealizeMessage);
	}

	// FVirtualLoopUpdateMessage
	FVirtualLoopUpdateMessage::FVirtualLoopUpdateMessage(const Trace::IAnalyzer::FOnEventContext& InContext)
		: FVirtualLoopMessageBase(InContext)
	{
		const Trace::IAnalyzer::FEventData& EventData = InContext.EventData;

		TimeVirtualized = EventData.GetValue<float>("TimeVirtualized");
		PlaybackTime = EventData.GetValue<float>("PlaybackTime");
		UpdateInterval = EventData.GetValue<float>("UpdateInterval");

		LocationX = EventData.GetValue<double>("LocationX");
		LocationY = EventData.GetValue<double>("LocationY");
		LocationZ = EventData.GetValue<double>("LocationZ");

		RotatorPitch = EventData.GetValue<double>("RotatorPitch");
		RotatorYaw = EventData.GetValue<double>("RotatorYaw");
		RotatorRoll = EventData.GetValue<double>("RotatorRoll");
	}

	uint32 FVirtualLoopUpdateMessage::GetSizeOf() const
	{
		// Returning fixed size of the struct (no dynamically sized members in it)
		return sizeof(FVirtualLoopUpdateMessage);
	}
} // namespace UE::Audio::Insights
