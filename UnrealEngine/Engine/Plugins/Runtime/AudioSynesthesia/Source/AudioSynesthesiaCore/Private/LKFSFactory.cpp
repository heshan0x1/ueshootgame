// Copyright Epic Games, Inc. All Rights Reserved.

#include "LKFSFactory.h"

#include "Algo/IsSorted.h"
#include "AudioSynesthesiaCoreLog.h"
#include "Containers/Array.h"
#include "DSP/SlidingWindow.h"
#include "LKFSPrivate.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LKFSFactory)

namespace Audio
{
	namespace LKFSFactoryPrivate
	{
		float LKFSToEnergy(float InLKFS)
		{
			return FMath::Pow(10, (InLKFS + 0.691f) / 10.f);
		}

		// Calculates average value from a circular buffer. Allows for interleaved data
		// by providing a "stride". 
		float CalculateAverageFromHistory(const TCircularBuffer<float>& InHistory, int32 InStartIndex, int32 InEndIndex, int32 InStride)
		{
			float Accum = 0.f;
			for (int32 Index = InStartIndex; Index < InEndIndex; Index += InStride)
			{
				Accum += InHistory[Index];
			}
			int32 Num = (InEndIndex - InStartIndex) / InStride;
			if (Num > 0)
			{
				return Accum /= Num;
			}
			return 0.f;
		}

		// Calculates gated average value from a circular buffer. Allows for interleaved data
		// by providing a "stride". 
		float CalculateGatedAverageFromHistory(const TCircularBuffer<float>& InHistory, int32 InStartIndex, int32 InEndIndex, int32 InStride, float InGate)
		{
			float Accum = 0.f;
			int32 Num = 0;
			for (int32 Index = InStartIndex; Index < InEndIndex; Index += InStride)
			{
				float Val = InHistory[Index];
				if (Val > InGate)
				{
					Accum += Val;
					Num++;
				}
			}
			if (Num > 0)
			{
				return Accum /= Num;
			}
			return InGate;
		}

		float CalculateGatedLoudnessFromHistory(const TCircularBuffer<float>& InHistory, int32 InStartIndex, int32 InEndIndex, int32 InStride)
		{
			// Gated Loudness only examines loudnesses above a certain gating loudness. 
			//
			// It is a 2 pass algorithm 
			// Pass 1: Use a set loudness threshold of -70lkfs to determine an initial gated loudness
			// Pass 2: Use the initial gated loudness to derive a new loudness threshold and calculate the gate loudness again. 
			constexpr float InitLoudnessThreshold = -70.f;
			float InitEnergyThreshold = LKFSToEnergy(InitLoudnessThreshold);

			const float GatedEnergyPass1 = CalculateGatedAverageFromHistory(InHistory, InStartIndex, InEndIndex, InStride, InitEnergyThreshold);

			const float NewThreshold = GatedEnergyPass1 * 0.1f;
			const float GatedEnergyPass2 = CalculateGatedAverageFromHistory(InHistory, InStartIndex, InEndIndex, InStride, NewThreshold);
			return FLoudnessAnalyzer::ConvertPerceptualEnergyToLKFS(GatedEnergyPass2);
		}
	}
}

namespace Audio
{
	// Use for overall loudness storage
	const int32 FLKFSAnalyzerResult::ChannelIndexOverall = -1;

	FLKFSAnalyzerResult::FLKFSAnalyzerResult()
	{
	}

	int32 FLKFSAnalyzerResult::GetNumChannels() const
	{
		int32 Num = ChannelResults.Num();
		if (Num > 0)
		{
			return Num - 1; // Subtract one for storage of "Overall" channel
		}
		return 0;
	}

	void FLKFSAnalyzerResult::Add(FLKFSResults InDatum)
	{
		TArray<FLKFSResults>& ChannelData = ChannelResults.FindOrAdd(InDatum.Channel);
		ChannelData.Add(MoveTemp(InDatum));
	}

	const TArray<FLKFSResults>& FLKFSAnalyzerResult::GetChannelLoudnessResults(int32 ChannelIdx) const
	{
		if (const TArray<FLKFSResults>* ChannelData = ChannelResults.Find(ChannelIdx))
		{
			return *ChannelData;
		}
		else
		{
			static const TArray<FLKFSResults> EmptyArray;
			return EmptyArray;
		}
	}

	const TArray<FLKFSResults>& FLKFSAnalyzerResult::GetLoudnessResults() const
	{
		return GetChannelLoudnessResults(ChannelIndexOverall);
	}

	FLKFSWorker::FLKFSWorker(const FAnalyzerParameters& InParams, const FLKFSSettings& InAnalyzerSettings)
	: NumChannels(InParams.NumChannels)
	, SampleRate(InParams.SampleRate)
	, ChannelEnergyHistory(1)
	, OverallEnergyHistory(1)
	, Analyzer(InParams.SampleRate, InAnalyzerSettings.LoudnessAnalyzerSettings)
	{
		check(NumChannels > 0);
		check(SampleRate > 0.f);
		check(InAnalyzerSettings.ShortTermLoudnessDuration > 0.f);
		check(InAnalyzerSettings.IntegratedLoudnessDuration > 0.f);
		check(InAnalyzerSettings.IntegratedLoudnessAnalysisPeriod> 0.f);


		int32 NumSlidingWindowFrames = Analyzer.GetSettings().WindowSize;

		int32 NumAnalysisFrames = FMath::CeilToInt(InAnalyzerSettings.AnalysisWindowDuration * InParams.SampleRate);
		int32 NumAnalysisHopFrames = FMath::CeilToInt(InAnalyzerSettings.AnalysisPeriod * InParams.SampleRate);

		// There may be multiple calls to the LoudnessAnalyzer to produce a single 
		// FLoudnessDatum. The sliding window hop is tuned here so that it best 
		// matches the desired AnalysisPeriod while maintaining between a 25% to 
		// 75% window overlap.
		LKFSPrivate::TuneSlidingWindwoHopSize(NumAnalysisHopFrames, NumSlidingWindowFrames, NumSlidingWindowHopFrames, NumAnalysisHopWindows);

		// Determine how many windows to analyze per loudness result
		NumAnalysisWindows = FMath::Max(1, ((NumAnalysisFrames - (NumSlidingWindowFrames / 2)) / NumSlidingWindowHopFrames) + 1);

		// Include NumChannels in calculating window size because windows are generated 
		// with interleaved samples and deinterleaved later. 
		int32 NumSlidingWindowSamples = NumSlidingWindowFrames * NumChannels;
		int32 NumSlidingWindowHopSamples = NumSlidingWindowHopFrames * NumChannels;

		InternalBuffer = TSlidingBuffer<float>(NumSlidingWindowSamples, NumSlidingWindowHopSamples);
		InternalWindow.AddUninitialized(NumSlidingWindowSamples);
		ChannelPerceptualEnergy.AddZeroed(NumChannels * NumAnalysisWindows);
		OverallPerceptualEnergy.AddZeroed(NumAnalysisWindows);

		// Initialize settings for calculating aggregate stats
		const int32 NumResultHopFrames = NumAnalysisHopWindows * NumSlidingWindowHopFrames;
		NumShortTermHistoryResults = FMath::Max(1, FMath::DivideAndRoundUp((int32) (InAnalyzerSettings.ShortTermLoudnessDuration * SampleRate), NumResultHopFrames));
		NumIntegratedHistoryResults = FMath::Max(1, FMath::DivideAndRoundUp((int32) (InAnalyzerSettings.IntegratedLoudnessDuration * SampleRate), NumResultHopFrames));
		NumIntegratedHopHistoryResults = FMath::Max(1, FMath::DivideAndRoundUp((int32) (InAnalyzerSettings.IntegratedLoudnessAnalysisPeriod * SampleRate), NumResultHopFrames));

		const int32 MaxHistory = FMath::Max(NumIntegratedHistoryResults, NumShortTermHistoryResults);

		ChannelEnergyHistory = TCircularBuffer<float>(MaxHistory * NumChannels);
		OverallEnergyHistory = TCircularBuffer<float>(MaxHistory);

		for (int32 ChannelIdx = 0; ChannelIdx < NumChannels; ChannelIdx++)
		{
			AggregateLoudnessStats.Add(ChannelIdx);
		}
		AggregateLoudnessStats.Add(FLKFSAnalyzerResult::ChannelIndexOverall);
	}

	void FLKFSWorker::Analyze(TArrayView<const float> InAudio, IAnalyzerResult* OutResult) 
	{
		// Assume that outer layers ensured that this is of correct type.
		FLKFSAnalyzerResult* LoudnessResult = static_cast<FLKFSAnalyzerResult*>(OutResult);

		check(nullptr != LoudnessResult);
		
		TAutoSlidingWindow<float> SlidingWindow(InternalBuffer, InAudio, InternalWindow);

		AnalyzeSlidingWindow(SlidingWindow, *LoudnessResult);
	}

	void FLKFSWorker::AnalyzeSlidingWindow(TAutoSlidingWindow<float>& InSlidingWindow, FLKFSAnalyzerResult& OutResult)
	{
		check(NumAnalysisWindows > 0);
		check(NumAnalysisHopWindows > 0);
		
		for (const TArray<float>& Window : InSlidingWindow)
		{
			AnalyzeWindow(Window);

			if ((NumAnalyzedBuffers >= NumAnalysisWindows) && ((NumAnalyzedBuffers - NumAnalysisWindows) % NumAnalysisHopWindows) == 0)
			{
				AddCurrentLoudnessDataToResult(OutResult);
			}
		}
	}

	void FLKFSWorker::AnalyzeWindow(TArrayView<const float> InWindow)
	{
		const int32 WrappedWindowIdx = (NumAnalyzedBuffers % NumAnalysisWindows);
		const int32 ChannelPerceptualEnergyWritePos = WrappedWindowIdx * NumChannels;

		// Calculate perceptual energy
		OverallPerceptualEnergy[WrappedWindowIdx] = Analyzer.CalculatePerceptualEnergy(InWindow, NumChannels, MakeArrayView<float>(ChannelPerceptualEnergy.GetData() + ChannelPerceptualEnergyWritePos, NumChannels));

		// update counters
		NumAnalyzedBuffers++;
	}

	void FLKFSWorker::AddCurrentLoudnessDataToHistory()
	{
		// Compute loudness per channel and store history
		for (int32 ChannelIdx = 0; ChannelIdx < NumChannels; ChannelIdx++)
		{
			// Determine average perceptual energy over analysis window
			float PerceptualEnergy = 0.f;
			for (int32 Pos = ChannelIdx; Pos < ChannelPerceptualEnergy.Num(); Pos += NumChannels)
			{
				PerceptualEnergy += ChannelPerceptualEnergy[Pos];
			}
			PerceptualEnergy /= (float)NumAnalysisWindows;

			// Store perceptual energy in history 
			ChannelEnergyHistory[(NumChannels * NumEnergyHistory) + ChannelIdx] = PerceptualEnergy;
		}

		{
			// Compute loudness overall
			float PerceptualEnergy = 0.f;
			for (int32 Pos = 0; Pos < OverallPerceptualEnergy.Num(); Pos++)
			{
				PerceptualEnergy += OverallPerceptualEnergy[Pos];
			}
			PerceptualEnergy /= (float)NumAnalysisWindows;

			OverallEnergyHistory[NumEnergyHistory] = PerceptualEnergy;
		}
	
		NumEnergyHistory++;
	}

	void FLKFSWorker::AddCurrentLoudnessDataToResult(FLKFSAnalyzerResult& OutResult)
	{
		AddCurrentLoudnessDataToHistory();

		UpdateAggregateLoudnessStats();

		// Accounting for all the frames positions analyzed in creating the 
		// data, determine the center frame. 
		const int32 CenterFramePos = ((2 * NumAnalyzedBuffers - NumAnalysisWindows) * NumSlidingWindowHopFrames + Analyzer.GetSettings().WindowSize) / 2;
		const float Timestamp = CenterFramePos / SampleRate;


		// Create results and store
		for (int32 ChannelIdx = 0; ChannelIdx < NumChannels; ChannelIdx++)
		{
			int32 HistoryIndex = ((NumEnergyHistory - 1) * NumChannels) + ChannelIdx;
			
			float Energy = ChannelEnergyHistory[HistoryIndex];
			const FAggregateLoudnessStats& Stats = AggregateLoudnessStats[ChannelIdx];

			OutResult.Add(
				FLKFSResults 
				{
					.Channel=ChannelIdx, 
					.Timestamp=Timestamp, 
					.Energy=Energy, 
					.Loudness=FLoudnessAnalyzer::ConvertPerceptualEnergyToLKFS(Energy),
					.ShortTermLoudness=Stats.ShortTermLoudness, 
					.IntegratedLoudness=Stats.IntegratedLoudness,
					.GatedLoudness=Stats.GatedLoudness
				}
			);
		}
		{
			int32 HistoryIndex = (NumEnergyHistory - 1);
			
			float Energy = OverallEnergyHistory[HistoryIndex];
			const FAggregateLoudnessStats& Stats = AggregateLoudnessStats[FLKFSAnalyzerResult::ChannelIndexOverall];

			OutResult.Add(
				FLKFSResults 
				{
					.Channel=FLKFSAnalyzerResult::ChannelIndexOverall, 
					.Timestamp=Timestamp, 
					.Energy=Energy, 
					.Loudness=FLoudnessAnalyzer::ConvertPerceptualEnergyToLKFS(Energy),
					.ShortTermLoudness=Stats.ShortTermLoudness, 
					.IntegratedLoudness=Stats.IntegratedLoudness,
					.GatedLoudness=Stats.GatedLoudness
				}
			);
		}
	}

	void FLKFSWorker::UpdateAggregateLoudnessStats()
	{
		// Short term loudness is always updated, but integrated & gated loudness are 
		// updated at a lower rate. 
		bool bUpdateIntegratedLoudness = (NumEnergyHistory == 1) || (NumEnergyHistory % NumIntegratedHopHistoryResults) == 0;


		// Per channel results
		for (int32 ChannelIdx = 0; ChannelIdx < NumChannels; ChannelIdx++)
		{
			FAggregateLoudnessStats& Stats = AggregateLoudnessStats[ChannelIdx];
			UpdateAggregateLoudnessStats(ChannelEnergyHistory, ChannelIdx, NumChannels, bUpdateIntegratedLoudness, Stats);
		}

		// Overall results
		{
			FAggregateLoudnessStats& Stats = AggregateLoudnessStats[FLKFSAnalyzerResult::ChannelIndexOverall];
			UpdateAggregateLoudnessStats(OverallEnergyHistory, 0 /*offset*/, 1/*stride*/, bUpdateIntegratedLoudness, Stats);
		}
	}

	void FLKFSWorker::UpdateAggregateLoudnessStats(const TCircularBuffer<float>& InHistory, int32 InOffset, int32 InStride, bool bInUpdateIntegratedLoudness, FAggregateLoudnessStats& OutStats) const
	{
		const int32 EndIndex = (NumEnergyHistory * InStride) + InOffset;

		// Update short term loudness always.
		{
			const int32 StartIndex = InOffset + FMath::Max(0, (NumEnergyHistory - NumShortTermHistoryResults) * InStride);
			float ShortTermAverageEnergy = LKFSFactoryPrivate::CalculateAverageFromHistory(InHistory, StartIndex, EndIndex, InStride);
			OutStats.ShortTermLoudness = FLoudnessAnalyzer::ConvertPerceptualEnergyToLKFS(ShortTermAverageEnergy);
		}

		// Only update long term loudness if flag is set. 
		if (bInUpdateIntegratedLoudness)
		{
			const int32 StartIndex = InOffset + FMath::Max(0, (NumEnergyHistory - NumIntegratedHistoryResults * InStride));

			float IntegratedEnergy = LKFSFactoryPrivate::CalculateAverageFromHistory(InHistory, StartIndex, EndIndex, InStride);
			OutStats.IntegratedLoudness = FLoudnessAnalyzer::ConvertPerceptualEnergyToLKFS(IntegratedEnergy);
			OutStats.GatedLoudness = LKFSFactoryPrivate::CalculateGatedLoudnessFromHistory(InHistory, StartIndex, EndIndex, InStride);
		}
	}

	/** Name of specific analyzer type. */
	FName FLKFSFactory::GetName() const 
	{
		static FName FactoryName(TEXT("LKFSFactory"));
		return FactoryName;
	}

	/** Human readable name of analyzer. */
	FString FLKFSFactory::GetTitle() const
	{
		return TEXT("LKFS Analyzer Real-Time");
	}

	TUniquePtr<IAnalyzerResult> FLKFSFactory::NewResult() const
	{
		TUniquePtr<FLKFSAnalyzerResult> Result = MakeUnique<FLKFSAnalyzerResult>();
		return Result;
	}

	TUniquePtr<IAnalyzerWorker> FLKFSFactory::NewWorker(const FAnalyzerParameters& InParams, const IAnalyzerSettings* InSettings) const
	{
		const FLKFSSettings* LoudnessSettings = static_cast<const FLKFSSettings*>(InSettings);

		check(nullptr != LoudnessSettings);

		return MakeUnique<FLKFSWorker>(InParams, *LoudnessSettings);
	}
}

