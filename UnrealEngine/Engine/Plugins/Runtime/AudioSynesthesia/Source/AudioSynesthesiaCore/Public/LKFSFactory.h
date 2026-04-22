// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/CircularBuffer.h"
#include "Containers/SortedMap.h"
#include "DSP/SlidingWindow.h"
#include "LoudnessAnalyzer.h"
#include "IAudioAnalyzerInterface.h"

#include "LKFSFactory.generated.h"

namespace Audio { template <typename InSampleType> class TSlidingBuffer; }

/** Data representing the instantaneous loudness of a audio. */
USTRUCT(BlueprintType)
struct FLKFSResults
{
	GENERATED_BODY()

	/** The audio channel index which produced this result. If -1, then
	 * the data was produced by combining all other audio channels */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	int32 Channel = 0;

	/** Time in seconds of the source audio which corresponds to the loudness measurements. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float Timestamp = 0.f;

	/** The instantaneous, perceptually weighted energy relative to full scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float Energy = 0.f;

	/** The instantaneous, perceptually weighted loudness in dB. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float Loudness = 0.f;

	/** The average loudness over a short term window of time in dB. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float ShortTermLoudness = 0.f;

	/** The average loudness over a long term window of time in dB. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float IntegratedLoudness = 0.f;

	/** The average gated loudness over a long term window of time in dB. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AudioAnalyzer)
	float GatedLoudness = 0.f;
};


namespace Audio
{
	/**
	 * Contains settings for loudness analyzer.
	 */
	class FLKFSSettings : public IAnalyzerSettings 
	{
	public:
		// These values are taken from the suggested standards for LKFS/LUFS defined
		// in ITU-R BS.1770
		static constexpr float StandardShortTermLoudnessDuration = 3.f; // 3 seconds
		static constexpr float StandardIntegratedLoudnessAnalysisPeriod = 1.f; // 1 second
																			   //
		// Streaming audio does not have a natural end point. Integrated loudness
		// in this implementation is taken over a long duration (as opposed to 
		// accumulating indefinitely). 
		static constexpr float DefaultIntegratedLoudnessDuration = 60.f; // 1 minute

		FLoudnessAnalyzerSettings LoudnessAnalyzerSettings = { 
			.FFTSize = 2048,
			.WindowType = EWindowType::Blackman,
			.LoudnessCurveType = ELoudnessCurveType::K,
			.MinAnalysisFrequency = 20.f,
			.MaxAnalysisFrequency = 20000.f,
			.ScalingMethod = ELoudnessAnalyzerScalingMethod::Corrected
		};

		/** Number of seconds between loudness measurements */
		float AnalysisPeriod = 0.1f;

		/** Number of seconds of audio analyzed for each loudness measurements */
		float AnalysisWindowDuration = 0.4f;

		/** Duration of audio analyzed for short term loudness. */
		float ShortTermLoudnessDuration = StandardShortTermLoudnessDuration;

		/** Number of seconds between long term loudness updates. */
		float IntegratedLoudnessAnalysisPeriod = StandardIntegratedLoudnessAnalysisPeriod;

		/** Duration of audio analyzed for long term loudness. */
		float IntegratedLoudnessDuration = DefaultIntegratedLoudnessDuration;
	};


	/** FLKFSResult contains the temporal evolution of loudness. */
	class FLKFSAnalyzerResult : public IAnalyzerResult
	{
	public:
		/** Denotes the overall loudness channel index as opposed individual channel indices. */
		AUDIOSYNESTHESIACORE_API static const int32 ChannelIndexOverall;

		AUDIOSYNESTHESIACORE_API FLKFSAnalyzerResult();

		/** The number of individual channels available in the result. */
		AUDIOSYNESTHESIACORE_API int32 GetNumChannels() const;

		/** Add to the result. */
		AUDIOSYNESTHESIACORE_API void Add(FLKFSResults InResults);

		/** Returns const reference to FLKFSResults array for individual channel. */
		AUDIOSYNESTHESIACORE_API const TArray<FLKFSResults>& GetChannelLoudnessResults(int32 ChannelIdx) const;

		/** Returns const reference to FLKFSResults array associated with overall loudness. */
		AUDIOSYNESTHESIACORE_API const TArray<FLKFSResults>& GetLoudnessResults() const;

	private:
		TSortedMap<int32, TArray<FLKFSResults>> ChannelResults;
	};

	/** 
	 * FLKFSWorker performs loudness analysis on input sample buffers.
	 */
	class FLKFSWorker : public IAnalyzerWorker
	{
	public:
		/** Construct a worker */
		AUDIOSYNESTHESIACORE_API FLKFSWorker(const FAnalyzerParameters& InParams, const FLKFSSettings& InAnalyzerSettings);

		/**
		 * Analyzes input sample buffer and updates result. 
		 */
		AUDIOSYNESTHESIACORE_API virtual void Analyze(TArrayView<const float> InAudio, IAnalyzerResult* OutResult) override;

	private:
		struct FAggregateLoudnessStats
		{
			static constexpr float InvalidLoudness = TNumericLimits<float>::Lowest();

			float ShortTermLoudness = InvalidLoudness;
			float IntegratedLoudness = InvalidLoudness;
			float GatedLoudness = InvalidLoudness;
		};


		void TuneSlidingWindwoHopSize(const int32 InAnalysisHopFrames, const int32 InWindowFrames, int32& OutBestWindowHopFrames, int32& OutBestAnalysisHopWindows) const;
		void AnalyzeSlidingWindow(TAutoSlidingWindow<float>& InSlidingWindow, FLKFSAnalyzerResult& OutResult);
		void AnalyzeWindow(TArrayView<const float> InWindow);

		void AddCurrentLoudnessDataToHistory();
		void AddCurrentLoudnessDataToResult(FLKFSAnalyzerResult& OutResult);
		void UpdateAggregateLoudnessStats();
		void UpdateAggregateLoudnessStats(const TCircularBuffer<float>& InHistory, int32 InOffset, int32 InStride, bool bInUpdateIntegratedLoudness, FAggregateLoudnessStats& OutStats) const;


		int32 NumChannels = 0;
		int32 NumAnalyzedBuffers = 0;
		int32 NumEnergyHistory = 0;
		int32 NumSlidingWindowHopFrames = 0;
		float SampleRate = 0.f;

		// Number of windows between each loudness analysis
		int32 NumAnalysisHopWindows = 1;
		// Number of windows inspected for each loudness analysis
		int32 NumAnalysisWindows = 1;
		// Number of values in the "EnergyHistory" to analyze for short term loudness
		int32 NumShortTermHistoryResults = 1;
		// Number of values in the "EnergyHistory" to analyze for integrated and gated loudness
		int32 NumIntegratedHistoryResults= 1;
		// Number of values in the "EnergyHistory" between integrated and gated loudness anlysis
		int32 NumIntegratedHopHistoryResults = 1;

		TArray<float> ChannelPerceptualEnergy;
		TArray<float> OverallPerceptualEnergy;
		TArray<float> InternalWindow;

		// Per channel stats for short-term, integrated and gated loudness. 
		TSortedMap<int32, FAggregateLoudnessStats> AggregateLoudnessStats;

		// Channel interleaved history buffer of energy measurements. These are 
		// taken once per an loudness analysis, not necessarily once per a audio window. 
		TCircularBuffer<float> ChannelEnergyHistory;
		TCircularBuffer<float> OverallEnergyHistory;

		TSlidingBuffer<float> InternalBuffer;
		FMultichannelLoudnessAnalyzer Analyzer;
	};

	/**
	 * Defines the LKFS analyzer and creates related classes.
	 */
	class FLKFSFactory : public IAnalyzerFactory
	{
	public:

		/** Name of specific analyzer type. */
		AUDIOSYNESTHESIACORE_API virtual FName GetName() const override;

		/** Human readable name of analyzer. */
		AUDIOSYNESTHESIACORE_API virtual FString GetTitle() const override;

		/** Creates a new FLKFSAnalyzerResult */
		AUDIOSYNESTHESIACORE_API virtual TUniquePtr<IAnalyzerResult> NewResult() const override;

		/** Creates a new FLKFSWorker. This expects IAnalyzerSettings to be a valid pointer to a FLKFSSettings object. */
		AUDIOSYNESTHESIACORE_API virtual TUniquePtr<IAnalyzerWorker> NewWorker(const FAnalyzerParameters& InParams, const IAnalyzerSettings* InSettings) const override;
	};
}
