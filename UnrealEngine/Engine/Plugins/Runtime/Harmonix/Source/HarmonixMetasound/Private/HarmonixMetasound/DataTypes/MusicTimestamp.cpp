// Copyright Epic Games, Inc. All Rights Reserved.

#include "HarmonixMetasound/DataTypes/MusicTimestamp.h"

#include "HarmonixMidi/BarMap.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MusicTimestamp)

bool UMusicTimestampBlueprintLibrary::IsMusicTimestamp(const FMetaSoundOutput& Output)
{
	return Output.IsType<FMusicTimestamp>();
}

FMusicTimestamp UMusicTimestampBlueprintLibrary::GetMusicTimestamp(const FMetaSoundOutput& Output, bool& Success)
{
	FMusicTimestamp Timestamp;
	Success = Output.Get(Timestamp);
	return Timestamp;
}

REGISTER_METASOUND_DATATYPE(FMusicTimestamp, "MusicTimestamp")
