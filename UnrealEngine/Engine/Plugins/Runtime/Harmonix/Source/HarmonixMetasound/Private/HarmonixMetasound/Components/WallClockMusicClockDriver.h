// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "HarmonixMetasound/Components/MusicClockDriverBase.h"
#include "HarmonixMidi/MidiFile.h"
#include "UObject/StrongObjectPtr.h"

class FWallClockMusicClockDriver : public FMusicClockDriverBase
{
public:
	FWallClockMusicClockDriver(UObject* WorldContextObj, const FWallClockMusicClockSettings& Settings)
		: FMusicClockDriverBase(WorldContextObj, Settings)
		, TempoMapMidi(Settings.TempoMap)
	{}

	virtual void Disconnect() override;
	virtual bool RefreshCurrentSongPos() override;
	virtual void OnStart() override;
	virtual void OnPause() override;
	virtual void OnContinue() override;
	virtual void OnStop() override {}
	virtual const ISongMapEvaluator* GetCurrentSongMapEvaluator() const override;
	
	virtual bool LoopedThisFrame(ECalibratedMusicTimebase Timebase) const override { return false; }
	virtual bool SeekedThisFrame(ECalibratedMusicTimebase Timebase) const override { return false; }

private:
	TWeakObjectPtr<UMidiFile> TempoMapMidi;

	double StartTimeSecs = 0.0;
	double PauseTimeSecs = 0.0f;
};