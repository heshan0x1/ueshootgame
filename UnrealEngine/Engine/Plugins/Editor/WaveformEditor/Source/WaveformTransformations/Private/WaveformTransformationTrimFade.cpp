// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformTransformationTrimFade.h"

#include "Editor.h"
#include "Sound/SoundWave.h"
#include "WaveformTransformationLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WaveformTransformationTrimFade)

namespace
{
	// Makes the two functions for SCurve meet at (0.5, 0.5)
	const double XMultiplier = FMath::Sqrt(10.0) / 5.0;

	UE_DEPRECATED(5.7, "Individual fade properties have been deprecated. Use FadeFunctions::FadeIn::GetFadeInCurveValue instead.")
	void SetFadeCurve(TObjectPtr<UFadeFunction> FadeFunction, float FadeCurve, float Sharpness)
	{
		if (FadeFunction->IsA<UFadeCurveFunction>())
		{
			TObjectPtr<UFadeCurveFunction> CurveFunction = CastChecked<UFadeCurveFunction>(FadeFunction);

			if (CurveFunction)
			{
				if (!CurveFunction->IsA<UFadeCurveFunctionSigmoid>())
				{
					CurveFunction->SetFadeCurve(FadeCurve);
				}
				else
				{
					CurveFunction->SetFadeCurve(Sharpness);
				}
			}
			
		}
	}
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const TMap<EWaveEditorFadeMode, float> UWaveformTransformationTrimFade::FadeModeToCurveValueMap =
{
	{EWaveEditorFadeMode::Linear, 1.0f},
	{EWaveEditorFadeMode::Exponential, 3.0f},
	{EWaveEditorFadeMode::Logarithmic, 0.25f},
	{EWaveEditorFadeMode::Sigmoid, UWaveformTransformationTrimFade::MinFadeCurve}
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

const TMap<EWaveEditorFadeMode, TSubclassOf<UFadeFunction>> UWaveformTransformationTrimFade::FadeModeToFadeFunctionMap =
{
	{EWaveEditorFadeMode::Linear, UFadeFunctionLinear::StaticClass()},
	{EWaveEditorFadeMode::Exponential, UFadeCurveFunctionExponential::StaticClass()},
	{EWaveEditorFadeMode::Logarithmic, UFadeCurveFunctionLogarithmic::StaticClass()},
	{EWaveEditorFadeMode::Sigmoid, UFadeCurveFunctionSigmoid::StaticClass()}
};

const float UWaveformTransformationTrimFade::MinFadeCurve = -0.1f;
const float UWaveformTransformationTrimFade::MaxFadeCurve = 10.f;

namespace WaveformTransformationTrimFadeNames
{
	static FLazyName StartTimeName(GET_MEMBER_NAME_CHECKED(UWaveformTransformationTrimFade, StartTime));
	static FLazyName EndTimeName(GET_MEMBER_NAME_CHECKED(UWaveformTransformationTrimFade, EndTime));
}

static void ApplyFadeIn(Audio::FAlignedFloatBuffer& InputAudio, const FFadeFunctionData& FadeFunctions, const int32 NumChannels, const float SampleRate)
{
	if (FadeFunctions.FadeIn == nullptr)
	{
		return;
	}

	check(NumChannels > 0);
	check(SampleRate > 0.f);

	const float FadeDuration = FadeFunctions.FadeIn->Duration;
	const int32 FadeNumFrames = FMath::Min(FadeDuration * SampleRate, InputAudio.Num() / NumChannels);

	if (InputAudio.Num() < NumChannels || FadeDuration < SMALL_NUMBER || FadeNumFrames == 0)
	{
		return;
	}

	float* InputPtr = InputAudio.GetData();

	for (int32 FrameIndex = 0; FrameIndex < FadeNumFrames; ++FrameIndex)
	{
		const float FadeFraction = static_cast<float>(FrameIndex) / static_cast<float>(FadeNumFrames);
		check(FadeFraction <= 1.0f);
		check(FadeFraction >= 0.0f);

		const float EnvValue = FadeFunctions.FadeIn->GetFadeInCurveValue(FadeFraction);

		for (int32 ChannelIt = 0; ChannelIt < NumChannels; ++ChannelIt)
		{
			*(InputPtr + ChannelIt) *= EnvValue;
		}

		InputPtr += NumChannels;
	}
}

static void ApplyFadeOut(Audio::FAlignedFloatBuffer& InputAudio, const FFadeFunctionData& FadeFunctions, const int32 NumChannels, const float SampleRate)
{
	if (FadeFunctions.FadeOut == nullptr)
	{
		return;
	}

	check(NumChannels > 0);
	check(SampleRate > 0.f);

	const float FadeDuration = FadeFunctions.FadeOut->Duration;
	const int32 FadeNumFrames = FMath::Min(FadeDuration * SampleRate, InputAudio.Num() / NumChannels);

	if (InputAudio.Num() < NumChannels || FadeDuration < SMALL_NUMBER || FadeNumFrames == 0)
	{
		return;
	}

	const int32 StartSampleIndex = InputAudio.Num() - (FadeNumFrames * NumChannels);
	float* InputPtr = &InputAudio[StartSampleIndex];

	for (int32 FrameIndex = 0; FrameIndex < FadeNumFrames; ++FrameIndex)
	{
		const float FadeFraction = static_cast<float>(FrameIndex) / static_cast<float>(FadeNumFrames);
		check(FadeFraction <= 1.0f);
		check(FadeFraction >= 0.0f);

		const float EnvValue = FadeFunctions.FadeOut->GetFadeOutCurveValue(FadeFraction);

		for (int32 ChannelIt = 0; ChannelIt < NumChannels; ++ChannelIt)
		{
			*(InputPtr + ChannelIt) *= EnvValue;
		}

		InputPtr += NumChannels;
	}
}

FWaveTransformationTrimFade::FWaveTransformationTrimFade(const FFadeFunctionData& InFadeFunctions, double InStartTime, double InEndTime)
	: FadeFunctions(InFadeFunctions)
	, StartTime(InStartTime)
	, EndTime(InEndTime){}

void FWaveTransformationTrimFade::ProcessAudio(Audio::FWaveformTransformationWaveInfo& InOutWaveInfo) const
{
	check(InOutWaveInfo.SampleRate > 0.f && InOutWaveInfo.Audio != nullptr);

	Audio::FAlignedFloatBuffer& InputAudio = *InOutWaveInfo.Audio;

	// If InputAudio is smaller than the size of a frame, do not process the audio.
	check(InOutWaveInfo.NumChannels > 0);

	int64 InputAudioNum = static_cast<int64>(InputAudio.Num());
	const int64 ExtraSamples = InputAudioNum % InOutWaveInfo.NumChannels;
	if (ExtraSamples > 0)
	{
		InputAudioNum -= ExtraSamples; // trim out samples beyond last full frame

		UE_LOG(LogWaveformTransformation, Log, TEXT("Invalid number of Samples, number of samples not divisible by the channel count."));
	}

	if (InputAudioNum < InOutWaveInfo.NumChannels)
	{
		return;
	}

	const int64 LastInputAudioIndex = static_cast<int64>(InputAudioNum - 1);
	const int64 NumChannelsMinusOne = static_cast<int64>(InOutWaveInfo.NumChannels - 1); // Used to step backwards from the end sample to a valid frame
	const int64 FirstSampleOfLastFrame = LastInputAudioIndex - NumChannelsMinusOne; // The last sample that begins a frame
	int64 StartSample = FMath::Min(FMath::FloorToInt64(StartTime * InOutWaveInfo.SampleRate) * InOutWaveInfo.NumChannels, FirstSampleOfLastFrame);
	int64 EndSample = LastInputAudioIndex;
	check(StartSample <= LastInputAudioIndex);
	
	if(EndTime > 0.f)
	{	
		const int64 EndFrame = FMath::RoundToInt64(EndTime * InOutWaveInfo.SampleRate);
		EndSample = EndFrame * InOutWaveInfo.NumChannels + NumChannelsMinusOne;
		EndSample = FMath::Min(EndSample, LastInputAudioIndex);
	}

	if (StartSample > EndSample - NumChannelsMinusOne)
	{
		StartSample = FMath::Max(EndSample - NumChannelsMinusOne, 0);
	}

	check(StartSample >= 0);
	check(StartSample % InOutWaveInfo.NumChannels == 0);
	check(EndSample >= StartSample);
	check((EndSample - NumChannelsMinusOne) % InOutWaveInfo.NumChannels == 0);

	const int64 FinalSize = (EndSample - StartSample) + 1;
	check(FinalSize % InOutWaveInfo.NumChannels == 0);

	InOutWaveInfo.StartFrameOffset = StartSample - (StartSample % InOutWaveInfo.NumChannels);
	InOutWaveInfo.NumEditedSamples = FinalSize;
	
	if (FinalSize > InputAudio.Num() || FinalSize <= 0)
	{
		return;
	}

	const bool bProcessFadeIn = FadeFunctions.FadeIn && FadeFunctions.FadeIn->Duration > 0.f;
	const bool bProcessFadeOut = FadeFunctions.FadeOut && FadeFunctions.FadeOut->Duration > 0.f;
	const bool bProcessFades = bProcessFadeIn || bProcessFadeOut;

	if (!bProcessFades && FinalSize == InputAudio.Num())
	{
		return;
	}

	TArray<float> TempBuffer;
	TempBuffer.AddUninitialized(FinalSize);

	FMemory::Memcpy(TempBuffer.GetData(), &InputAudio[StartSample], FinalSize * sizeof(float));

	InputAudio.Empty();
	InputAudio.AddUninitialized(FinalSize);

	FMemory::Memcpy(InputAudio.GetData(), TempBuffer.GetData(), FinalSize * sizeof(float));

	if (bProcessFadeIn)
	{
		ApplyFadeIn(InputAudio, FadeFunctions, InOutWaveInfo.NumChannels, InOutWaveInfo.SampleRate);
	}

	if (bProcessFadeOut)
	{
		ApplyFadeOut(InputAudio, FadeFunctions, InOutWaveInfo.NumChannels, InOutWaveInfo.SampleRate);
	}
}

UWaveformTransformationTrimFade::UWaveformTransformationTrimFade(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	const float Linear = 1.f;

	if (FadeFunctions.FadeIn == nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (StartFadeTime != 0.f)
			{
				if (StartFadeCurve > Linear)
				{
					FadeFunctions.FadeIn = CreateDefaultSubobject<UFadeCurveFunctionExponential>(TEXT("FadeInFunction"));
				}
				else if (StartFadeCurve < 0.f)
				{
					FadeFunctions.FadeIn = CreateDefaultSubobject<UFadeCurveFunctionSigmoid>(TEXT("FadeInFunction"));
				}
				else if (StartFadeCurve < Linear)
				{
					FadeFunctions.FadeIn = CreateDefaultSubobject<UFadeCurveFunctionLogarithmic>(TEXT("FadeInFunction"));
				}
				else if (StartFadeCurve == Linear)
				{
					FadeFunctions.FadeIn = CreateDefaultSubobject<UFadeFunctionLinear>(TEXT("FadeInFunction"));
				}

				FadeFunctions.FadeIn->Duration = StartFadeTime;
				SetFadeCurve(FadeFunctions.FadeIn, StartFadeCurve, StartSCurveSharpness);

				StartFadeTime = 0.f;
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
			else
			{
				FadeFunctions.FadeIn = CreateDefaultSubobject<UFadeCurveFunctionExponential>(TEXT("FadeInFunction"));
				FadeFunctions.FadeIn->Duration = 0.f;
			}
	}


	if (FadeFunctions.FadeOut == nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (EndFadeTime != 0.f)
			{
				if (EndFadeCurve > Linear)
				{
					FadeFunctions.FadeOut = CreateDefaultSubobject<UFadeCurveFunctionExponential>(TEXT("FadeOutFunction"));
				}
				else if (EndFadeCurve < 0.f)
				{
					FadeFunctions.FadeOut = CreateDefaultSubobject<UFadeCurveFunctionSigmoid>(TEXT("FadeOutFunction"));
				}
				else if (EndFadeCurve < Linear)
				{
					FadeFunctions.FadeOut = CreateDefaultSubobject<UFadeCurveFunctionLogarithmic>(TEXT("FadeOutFunction"));
				}
				else if (EndFadeCurve == Linear)
				{
					FadeFunctions.FadeOut = CreateDefaultSubobject<UFadeFunctionLinear>(TEXT("FadeOutFunction"));
				}

				FadeFunctions.FadeOut->Duration = EndFadeTime;
				SetFadeCurve(FadeFunctions.FadeOut, EndFadeCurve, EndSCurveSharpness);

				EndFadeTime = 0.f;
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
			else
			{
				FadeFunctions.FadeOut = CreateDefaultSubobject<UFadeCurveFunctionExponential>(TEXT("FadeOutFunction"));
				FadeFunctions.FadeOut->Duration = 0.f;
			}
	}
}

#if WITH_EDITOR
void UWaveformTransformationTrimFade::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const float MaxDuration = EndTime - StartTime;

	if (FadeFunctions.FadeIn)
	{
		FadeFunctions.FadeIn->Duration = FMath::Min(FadeFunctions.FadeIn->Duration, MaxDuration);
	}

	if (FadeFunctions.FadeOut)
	{
		FadeFunctions.FadeOut->Duration = FMath::Min(FadeFunctions.FadeOut->Duration, MaxDuration);
	}
}
#endif // WITH_EDITOR

void UWaveformTransformationTrimFade::PostLoad()
{
	Super::PostLoad();

	const float Linear = 1.f;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Overwrite exiting fade in function data with deprecated curve properties to migrate the data to the new properties
	if (StartFadeTime != 0.f)
	{
		if (StartFadeCurve > Linear)
		{
			FadeFunctions.FadeIn = NewObject<UFadeCurveFunctionExponential>(this, UFadeCurveFunctionExponential::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (StartFadeCurve < 0.f)
		{
			FadeFunctions.FadeIn = NewObject<UFadeCurveFunctionSigmoid>(this, UFadeCurveFunctionSigmoid::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (StartFadeCurve < Linear)
		{
			FadeFunctions.FadeIn = NewObject<UFadeCurveFunctionLogarithmic>(this, UFadeCurveFunctionLogarithmic::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (StartFadeCurve == Linear)
		{
			FadeFunctions.FadeIn = NewObject<UFadeFunctionLinear>(this, UFadeFunctionLinear::StaticClass(), NAME_None, RF_Transactional);
		}

		FadeFunctions.FadeIn->Duration = StartFadeTime;
		SetFadeCurve(FadeFunctions.FadeIn, StartFadeCurve, StartSCurveSharpness);

		StartFadeTime = 0.f;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	else if (FadeFunctions.FadeIn == nullptr)
	{
		FadeFunctions.FadeIn = NewObject<UFadeCurveFunctionExponential>(this, UFadeCurveFunctionExponential::StaticClass(), NAME_None, RF_Transactional);
		FadeFunctions.FadeIn->Duration = 0.f;
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Overwrite exiting fade out function data with deprecated curve properties to migrate the data to the new properties
	if (EndFadeTime != 0.f)
	{
		if (EndFadeCurve > Linear)
		{
			FadeFunctions.FadeOut = NewObject<UFadeCurveFunctionExponential>(this, UFadeCurveFunctionExponential::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (EndFadeCurve < 0.f)
		{
			FadeFunctions.FadeOut = NewObject<UFadeCurveFunctionSigmoid>(this, UFadeCurveFunctionSigmoid::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (EndFadeCurve < Linear)
		{
			FadeFunctions.FadeOut = NewObject<UFadeCurveFunctionLogarithmic>(this, UFadeCurveFunctionLogarithmic::StaticClass(), NAME_None, RF_Transactional);
		}
		else if (EndFadeCurve == Linear)
		{
			FadeFunctions.FadeOut = NewObject<UFadeFunctionLinear>(this, UFadeFunctionLinear::StaticClass(), NAME_None, RF_Transactional);
		}

		FadeFunctions.FadeOut->Duration = EndFadeTime;
		SetFadeCurve(FadeFunctions.FadeOut, EndFadeCurve, EndSCurveSharpness);

		EndFadeTime = 0.f;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	else if (FadeFunctions.FadeOut == nullptr)
	{
		FadeFunctions.FadeOut = NewObject<UFadeCurveFunctionExponential>(this, UFadeCurveFunctionExponential::StaticClass(), NAME_None, RF_Transactional);
		FadeFunctions.FadeOut->Duration = 0.f;
	}
}

Audio::FTransformationPtr UWaveformTransformationTrimFade::CreateTransformation() const
{
	return MakeUnique<FWaveTransformationTrimFade>(FadeFunctions, StartTime, EndTime);
}

void UWaveformTransformationTrimFade::UpdateConfiguration(FWaveTransformUObjectConfiguration& InOutConfiguration)
{
	UpdateDurationProperties(InOutConfiguration.EndTime - InOutConfiguration.StartTime);

	InOutConfiguration.StartTime = StartTime;
	InOutConfiguration.EndTime = EndTime;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const double UWaveformTransformationTrimFade::GetFadeInCurveValue(const float StartFadeCurve, const double FadeFraction, const float SCurveSharpness)
{
	double CurveValue = 1;

	if (StartFadeCurve < 0)
	{
		const double X = FadeFraction;
		const double A = static_cast<float>(-FMath::Clamp(SCurveSharpness, 0.1f, 1.0f));
		const double K = 20 * FMath::Pow(A, 5.0);

		if (FadeFraction <= 0.5)
		{
			CurveValue = -(FMath::Tanh(K * PI * FMath::LogX(10, XMultiplier * X) + ((K * PI) / 2))) / 2 + 0.5;
		}
		else
		{
			CurveValue = (FMath::Tanh(K * PI * FMath::LogX(10, XMultiplier * (-X + 1)) + ((K * PI) / 2))) / 2 + 0.5;
		}
	}
	else
	{
		CurveValue = FMath::Pow(FadeFraction, StartFadeCurve);
	}

	return CurveValue;
}

const double UWaveformTransformationTrimFade::GetFadeOutCurveValue(const float EndFadeCurve, const double FadeFraction, const float SCurveSharpness)
{
	double CurveValue = 1;

	if (EndFadeCurve < 0)
	{
		const double X = FadeFraction;
		const double A = static_cast<float>(FMath::Clamp(SCurveSharpness, 0.1f, 1.0f));
		const double K = 20 * FMath::Pow(A, 5.0);

		if (FadeFraction <= 0.5)
		{
			CurveValue = -(FMath::Tanh(K * PI * FMath::LogX(10, XMultiplier * X) + ((K * PI) / 2))) / 2 + 0.5;
		}
		else
		{
			CurveValue = (FMath::Tanh(K * PI * FMath::LogX(10, XMultiplier * (-X + 1)) + ((K * PI) / 2))) / 2 + 0.5;
		}
	}
	else
	{
		// Changed from 1.f - FMath::Pow(FadeFraction, FadeCurve) so that the fade out curve is a horizontally mirrored version of fade in 
		// instead of vertically mirrored
		CurveValue = FMath::Pow(-FadeFraction + 1, EndFadeCurve);
	}

	return CurveValue;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UWaveformTransformationTrimFade::UpdateDurationProperties(const float InAvailableDuration)
{
	check(InAvailableDuration > 0)
	AvailableWaveformDuration = InAvailableDuration;
	StartTime = FMath::Clamp(StartTime, 0.f, AvailableWaveformDuration - UE_KINDA_SMALL_NUMBER);
	EndTime = EndTime < 0 ? EndTime = AvailableWaveformDuration : FMath::Clamp(EndTime, StartTime + UE_KINDA_SMALL_NUMBER, AvailableWaveformDuration);
}

const double UFadeFunctionLinear::GetFadeInCurveValue(const double FadeFraction) const
{
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return FadeFraction;
}

const double UFadeFunctionLinear::GetFadeOutCurveValue(const double FadeFraction) const
{
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return 1.0 - FadeFraction;
}

const double UFadeCurveFunctionExponential::GetFadeInCurveValue(const double FadeFraction) const
{
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return FMath::Pow(FadeFraction, FadeCurve);
}

const double UFadeCurveFunctionExponential::GetFadeOutCurveValue(const double FadeFraction) const
{
	// Pow functions return NaN for negative bases with non-integer exponents
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return FMath::Pow(-FadeFraction + 1.0, FadeCurve);
}

const double UFadeCurveFunctionLogarithmic::GetFadeInCurveValue(const double FadeFraction) const
{
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return FMath::Pow(FadeFraction, FadeCurve);
}

const double UFadeCurveFunctionLogarithmic::GetFadeOutCurveValue(const double FadeFraction) const
{
	// Pow functions return NaN for negative bases with non-integer exponents
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);

	return FMath::Pow(-FadeFraction + 1.0, FadeCurve);
}

const double UFadeCurveFunctionSigmoid::GetFadeInCurveValue(const double FadeFraction) const
{
	// LogX will produce NaN if FadeFraction is <= 0 or is 1
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);
	check(XMultiplier > 0.0);

	if (FadeFraction == 0.f || FadeFraction == 1.f)
	{
		return FadeFraction;
	}

	double CurveValue = 1;
	const double x = FadeFraction;
	const double a = static_cast<float>(-FMath::Clamp(SFadeCurve, 0.1f, 1.0f));
	const double k = 20 * FMath::Pow(a, 5);

	if (FadeFraction <= 0.5)
	{
		CurveValue = -(FMath::Tanh(k * PI * FMath::LogX(10, XMultiplier * x) + ((k * PI) / 2))) / 2 + 0.5;
	}
	else
	{
		CurveValue = (FMath::Tanh(k * PI * FMath::LogX(10, XMultiplier * (-x + 1)) + ((k * PI) / 2))) / 2 + 0.5;
	}

	return CurveValue;
}

const double UFadeCurveFunctionSigmoid::GetFadeOutCurveValue(const double FadeFraction) const
{
	// LogX will produce NaN if FadeFraction is <= 0 or is 1
	check(FadeFraction <= 1.f);
	check(FadeFraction >= 0.f);
	check(XMultiplier > 0.0);

	if (FadeFraction == 0.f || FadeFraction == 1.f)
	{
		return 1.0 - FadeFraction;
	}

	double CurveValue = 1;
	const double x = FadeFraction;
	const double a = static_cast<float>(FMath::Clamp(SFadeCurve, 0.1f, 1.0f));
	const double k = 20 * FMath::Pow(a, 5);

	if (FadeFraction <= 0.5)
	{
		CurveValue = -(FMath::Tanh(k * PI * FMath::LogX(10, XMultiplier * x) + ((k * PI) / 2))) / 2 + 0.5;
	}
	else
	{
		CurveValue = (FMath::Tanh(k * PI * FMath::LogX(10, XMultiplier * (-x + 1)) + ((k * PI) / 2))) / 2 + 0.5;
	}

	return CurveValue;
}
