// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MidiClock.h"

namespace Harmonix
{
	struct FMusicTimeInterval
	{
		EMidiClockSubdivisionQuantization Interval = EMidiClockSubdivisionQuantization::Beat;
		EMidiClockSubdivisionQuantization Offset = EMidiClockSubdivisionQuantization::Beat;
		uint16 IntervalMultiplier = 1;
		uint16 OffsetMultiplier = 0;

		HARMONIXMETASOUND_API float GetIntervalBeats(const FTimeSignature& TimeSignature) const;

		HARMONIXMETASOUND_API float GetOffsetBeats(const FTimeSignature& TimeSignature) const;
	};

	HARMONIXMETASOUND_API void IncrementTimestampByInterval(
			FMusicTimestamp& Timestamp,
			const FMusicTimeInterval& Interval,
			const FTimeSignature& TimeSignature);

	HARMONIXMETASOUND_API void IncrementTimestampByOffset(
			FMusicTimestamp& Timestamp,
			const FMusicTimeInterval& Interval,
			const FTimeSignature& TimeSignature);

	HARMONIXMETASOUND_API void IncrementTimestampByBeats(
		FMusicTimestamp& Timestamp,
		float Beats,
		const FTimeSignature& TimeSignature);
}
