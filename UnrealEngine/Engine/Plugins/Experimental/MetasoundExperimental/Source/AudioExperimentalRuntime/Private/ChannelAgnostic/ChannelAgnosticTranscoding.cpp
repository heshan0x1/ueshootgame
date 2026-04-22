// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChannelAgnostic/ChannelAgnosticTranscoding.h"

#include "Algo/Transform.h"
#include "DSP/Ambisonics.h"
#include "DSP/MultiMono.h"
#include "DSP/SphericalHarmonicCalculator.h"
#include "TypeFamily/ChannelTypeFamily.h"

namespace Audio
{
	static TArrayView<ESpeakerShortNames> GetUnrealOrder()
	{
		static ESpeakerShortNames Order[]
		{
			#define CSV(X) X,
			FOREACH_ENUM_ESPEAKERSHORTNAMES(CSV)
			#undef CSV
		};
		return Order;
	}

	static void TranslateGainMatrix(
		const TArrayView<float>& InGainMatrix,
		const int32 InNumInputChannels,
		const int32 InNumOutputChannels,
		const TArrayView<const ESpeakerShortNames>& InOldOrder,
		const TArrayView<const ESpeakerShortNames>& InNewOrder,
		TArray<float>& OutGains)
	{
		// Channels not found in new order will be zeroed.
		OutGains.SetNumZeroed(InNumOutputChannels*InNumInputChannels);
		
		// Lookup (old->new).
		int32 Lookup[static_cast<int32>(ESpeakerShortNames::NumChannels)] = { INDEX_NONE };
		for (int32 i = 0; i < InOldOrder.Num(); ++i)
		{
			Lookup[static_cast<int32>(InOldOrder[i])] = InNewOrder.IndexOfByKey(InOldOrder[i]);
		}
		
		// Translate.
		for (int32 InputChannelIndex=0; InputChannelIndex < InNumInputChannels; ++InputChannelIndex)
		{
			// This input channel has a mapping in the new?
			const ESpeakerShortNames OldIn = InOldOrder[InputChannelIndex];
			if (const int32 NewIn = Lookup[static_cast<int32>(OldIn)]; NewIn != INDEX_NONE)
			{
				for (int32 OutputChannelIndex=0; OutputChannelIndex < InNumOutputChannels; ++OutputChannelIndex)
				{
					const ESpeakerShortNames OldOut = InOldOrder[OutputChannelIndex];
					if (const int32 NewOut = Lookup[static_cast<int32>(OldOut)]; NewOut != INDEX_NONE)
					{
						OutGains[(NewIn				* InNumOutputChannels)		+ NewOut] =
					InGainMatrix[(InputChannelIndex * InNumOutputChannels)		+ OutputChannelIndex];
					}
				}
			}
		}
	}
	
	// Discrete to Discrete
	FChannelTypeFamily::FTranscoder GetTranscoder(
		const FDiscreteChannelTypeFamily& InSrcType, const FDiscreteChannelTypeFamily& InDstType, const FChannelTypeFamily::FGetTranscoderParams& InParams)
	{
		// Exact match? We can just memcpy each channel.
		// TODO. in future these could be shared-ptrs from the main CAT memory block.
		if (&InDstType == &InSrcType)
		{
			const int32 NumChannels = InDstType.NumChannels();
			return [NumChannels](TArrayView<const float*> Src, TArrayView<float*> Dst, const int32 NumFrames)
			{
				for (int32 i = 0; i < NumChannels; ++i)
				{
					FMemory::Memcpy(Dst[i], Src[i], NumFrames * sizeof(float));
				}
			};
		}

		switch (InParams.TranscodeMethod)
		{
			case EChannelTranscodeMethod::ChannelDrop:
			{
				return [&InSrcType,&InDstType](TArrayView<const float*> SrcChannels, TArrayView<float*> DstChannels, const int32 NumFrames)
				{
					// Copy everything destination wants, and nothing else.
					const TArray<FDiscreteChannelTypeFamily::FSpeaker>& SrcOrder = InSrcType.GetSpeakerOrder();
					const TArray<FDiscreteChannelTypeFamily::FSpeaker>& DstOrder = InDstType.GetSpeakerOrder();
					const int32 NumDstChannels = InDstType.NumChannels();	
					for (int32 i = 0; i < NumDstChannels; ++i)
					{
						const ESpeakerShortNames DstSpeaker = DstOrder[i].Speaker;
						if (const int32 SrcChannelIndex = SrcOrder.IndexOfByPredicate([&](const FDiscreteChannelTypeFamily::FSpeaker& j) { return j.Speaker == DstSpeaker; });
							SrcChannelIndex != INDEX_NONE)
						{
							FMemory::Memcpy(DstChannels[i], SrcChannels[SrcChannelIndex], NumFrames * sizeof(float));
						}
					}
				};
			}

			case EChannelTranscodeMethod::MixUpOrDown:
			{
				// Make a mix matrix and call a mix/up down.
				if (TArray<float> Gains; Create2DChannelMap(
					{
						.NumInputChannels = InSrcType.NumChannels(),
						.NumOutputChannels = InDstType.NumChannels(),
						.Order = EChannelMapOrder::OutputMajorOrder,
						.MonoUpmixMethod = InParams.MixMethod,
						.bIsCenterChannelOnly = InSrcType.NumChannels() == 1 && InSrcType.HasSpeaker(ESpeakerShortNames::FC)
					}, Gains))
				{
					TArray<float> TranslatedGains;
					TArray<ESpeakerShortNames> FromOrder;
					Algo::Transform(InSrcType.GetSpeakerOrder(), FromOrder, [](const FDiscreteChannelTypeFamily::FSpeaker& i) { return i.Speaker; });
					TranslateGainMatrix(Gains, InSrcType.NumChannels(), InDstType.NumChannels(), GetUnrealOrder(), FromOrder, TranslatedGains);
					return [MixGains = MoveTemp(TranslatedGains)](TArrayView<const float*> InSrc, TArrayView<float*> InDst, const int32 NumFrames)
					{
						MultiMonoMixUpOrDown(InSrc, InDst, NumFrames, MixGains);
					};
				}
			}
		}
		// fail.
		return {};
	}

	// Discrete to Ambisonics
	FChannelTypeFamily::FTranscoder GetTranscoder(const FDiscreteChannelTypeFamily& InFromType,
		const FAmbisonicsChannelTypeFamily& InToType, const FChannelTypeFamily::FGetTranscoderParams&)
	{
		const TArray<FDiscreteChannelTypeFamily::FSpeaker>& Speakers = InFromType.GetSpeakerOrder();
		TArray<TArray<float>> AllChannelGainArrays;
		AllChannelGainArrays.SetNum(InFromType.NumChannels());

		for (int32 i = 0; i < InFromType.NumChannels(); ++i)
		{
			const FDiscreteChannelTypeFamily::FSpeaker& Speaker = Speakers[i];
			
			TArray<float>& GainArray = AllChannelGainArrays[i];
			GainArray.SetNumZeroed(InToType.NumChannels());

			// Skip LFE.
			if (Speakers[i].Speaker == ESpeakerShortNames::LFE)
			{
				continue;
			}
			
			//FVector2f Spherical = Speakers[i].Position.UnitCartesianToSpherical();
			//FSphericalHarmonicCalculator::AdjustUESphericalCoordinatesForAmbisonics(Spherical);
			const float AzimuthRads = FMath::DegreesToRadians(Speaker.AzimuthDegrees);
			const float ElevationRads = FMath::DegreesToRadians(Speaker.ElevationDegrees);
			FSphericalHarmonicCalculator::ComputeSoundfieldChannelGains(InToType.GetAmbisonicsOrder(), AzimuthRads, ElevationRads, GainArray);
			FSphericalHarmonicCalculator::NormalizeGains(GainArray);
		}
	
		return [Gains = MoveTemp(AllChannelGainArrays)](TArrayView<const float*> Src, TArrayView<float*> Dst, const int32 NumFrames)
		{
			const int32 NumSrcChannels = Src.Num();
			for( int32 i = 0; i < Dst.Num(); ++i)
			{
				FMemory::Memzero(Dst[i], sizeof(float) * NumFrames);
			}
			for (int32 SourceChannelIndex = 0; SourceChannelIndex < NumSrcChannels; ++SourceChannelIndex)
			{
				EncodeMonoAmbisonicMixIn(MakeArrayView(Src[SourceChannelIndex], NumFrames), Dst, Gains[SourceChannelIndex]);
			}
		};
	}
	
	// Ambisonics to Discrete
	FChannelTypeFamily::FTranscoder GetTranscoder(const FAmbisonicsChannelTypeFamily& InFromType, const FDiscreteChannelTypeFamily& InToType, const FChannelTypeFamily::FGetTranscoderParams&)
	{
		TArray<TArray<float>> AllChannelGainArrays;
		AllChannelGainArrays.SetNum(InToType.NumChannels());
		const TArray<FDiscreteChannelTypeFamily::FSpeaker>& Order = InToType.GetSpeakerOrder();
		for (int32 i = 0; i < InToType.NumChannels(); ++i)
		{
			const FDiscreteChannelTypeFamily::FSpeaker& Speaker = Order[i];
			
			TArray<float>& GainArray = AllChannelGainArrays[i];
			GainArray.SetNum(InFromType.NumChannels());

			// Skip LFE.
			if (Order[i].Speaker == ESpeakerShortNames::LFE)
			{
				continue;
			}
			
			//FVector2f Spherical = Order[i].Position.UnitCartesianToSpherical();
			//FSphericalHarmonicCalculator::AdjustUESphericalCoordinatesForAmbisonics(Spherical);
			check(GainArray.Num() == FAmbisonicsChannelTypeFamily::OrderToNumChannels(InFromType.GetAmbisonicsOrder()));
			const float AzimuthRads = FMath::DegreesToRadians(Speaker.AzimuthDegrees);
			const float ElevationRads = FMath::DegreesToRadians(Speaker.ElevationDegrees);
			FSphericalHarmonicCalculator::ComputeSoundfieldChannelGains(InFromType.GetAmbisonicsOrder(), AzimuthRads, ElevationRads, GainArray);
			FSphericalHarmonicCalculator::NormalizeGains(GainArray);
		}
	
		return [NumSpeakers = InToType.NumChannels(), Gains = MoveTemp(AllChannelGainArrays)](TArrayView<const float*> Src, TArrayView<float*> Dst, const int32 NumFrames)
		{
			for (int32 i = 0; i < NumSpeakers; ++i)
			{
				TArrayView<float> DstChannel = MakeArrayView(Dst[i],NumFrames);
				FMemory::Memzero(DstChannel.GetData(), DstChannel.NumBytes()); // Decoder is MixIn.
				DecodeMonoAmbisonicMixIn(Src, DstChannel, Gains[i]);
			}
		};
	}

	// Ambisonics to Ambisonics
	FChannelTypeFamily::FTranscoder GetTranscoder(const FAmbisonicsChannelTypeFamily& InFromType,
		const FAmbisonicsChannelTypeFamily& InToType, const FChannelTypeFamily::FGetTranscoderParams& InParams)
	{
		// Ambisonics to Ambisonics
		if (&InToType == &InFromType)
		{
			// Same, so memcpy.
			return [](TArrayView<const float*> InSrc, TArrayView<float*> InDst, const int32 NumFrames)
			{
				for (int32 i = 0; i < InSrc.Num(); ++i)
				{
					FMemory::Memcpy(InDst[i], InSrc[i], sizeof(float) * NumFrames);
				}
			};
		}

		const int32 NumChannelsToCopy = FMath::Min(InToType.NumChannels(), InFromType.NumChannels());
		return [NumChannelsToCopy](TArrayView<const float*> InSrc, TArrayView<float*> InDst, const int32 NumFrames)
		{
			// Copy as many channels as destination will hold.
			for (int32 i = 0; i < NumChannelsToCopy; ++i)
			{
				FMemory::Memcpy(InDst[i], InSrc[i], sizeof(float) * NumFrames);
			}
		};
	}
}