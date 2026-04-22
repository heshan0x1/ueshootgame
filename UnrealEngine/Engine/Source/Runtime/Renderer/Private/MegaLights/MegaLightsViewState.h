// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector4.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

enum class EMegaLightsInput : uint8;

class FMegaLightsViewState
{
public:
	struct FResources
	{
		TRefCountPtr<IPooledRenderTarget> DiffuseLightingHistory;
		TRefCountPtr<IPooledRenderTarget> SpecularLightingHistory;
		TRefCountPtr<IPooledRenderTarget> LightingMomentsHistory;
		TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedHistory;
		TRefCountPtr<FRDGPooledBuffer> VisibleLightHashHistory;
		TRefCountPtr<FRDGPooledBuffer> VisibleLightMaskHashHistory;
		TRefCountPtr<FRDGPooledBuffer> VolumeVisibleLightHashHistory;
		TRefCountPtr<FRDGPooledBuffer> TranslucencyVolume0VisibleLightHashHistory;
		TRefCountPtr<FRDGPooledBuffer> TranslucencyVolume1VisibleLightHashHistory;

		// Optionally used, default is StochasticLightingViewState.SceneXxxHistory
		TRefCountPtr<IPooledRenderTarget> SceneDepthHistory;
		TRefCountPtr<IPooledRenderTarget> SceneNormalHistory;

		FIntPoint HistoryVisibleLightHashViewMinInTiles = 0;
		FIntPoint HistoryVisibleLightHashViewSizeInTiles = 0;

		FIntVector HistoryVolumeVisibleLightHashViewSizeInTiles = FIntVector::ZeroValue;
		FIntVector HistoryTranslucencyVolumeVisibleLightHashSizeInTiles = FIntVector::ZeroValue;

		void SafeRelease()
		{
			DiffuseLightingHistory.SafeRelease();
			SpecularLightingHistory.SafeRelease();
			LightingMomentsHistory.SafeRelease();
			NumFramesAccumulatedHistory.SafeRelease();
			VisibleLightHashHistory.SafeRelease();
			VisibleLightMaskHashHistory.SafeRelease();
			VolumeVisibleLightHashHistory.SafeRelease();
			TranslucencyVolume0VisibleLightHashHistory.SafeRelease();
			TranslucencyVolume1VisibleLightHashHistory.SafeRelease();
		}

		uint64 GetGPUSizeBytes(bool bLogSizes) const;
	};

	void SafeRelease()
	{
		GBuffer.SafeRelease();
		HairStrands.SafeRelease();
	}

	uint64 GetGPUSizeBytes(bool bLogSizes) const
	{
		uint64 Out = 0;
		Out += GBuffer.GetGPUSizeBytes(bLogSizes);
		Out += HairStrands.GetGPUSizeBytes(bLogSizes);
		return Out;
	}

	FResources GBuffer;
	FResources HairStrands;
};

