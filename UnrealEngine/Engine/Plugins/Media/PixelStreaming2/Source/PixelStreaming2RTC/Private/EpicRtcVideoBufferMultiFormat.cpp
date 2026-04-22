// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoBufferMultiFormat.h"

namespace UE::PixelStreaming2
{
	FEpicRtcVideoBufferMultiFormatBase::FEpicRtcVideoBufferMultiFormatBase(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer)
		: FrameCapturer(InFrameCapturer)
	{
	}

	FEpicRtcVideoBufferMultiFormatLayered::FEpicRtcVideoBufferMultiFormatLayered(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer, FIntPoint SourceResolution)
		: FEpicRtcVideoBufferMultiFormatBase(InFrameCapturer)
		, SourceResolution(SourceResolution)
	{
	}

	int FEpicRtcVideoBufferMultiFormatLayered::GetWidth()
	{
		return SourceResolution.X;
	}

	int FEpicRtcVideoBufferMultiFormatLayered::GetHeight()
	{
		return SourceResolution.Y;
	}

	TRefCountPtr<FEpicRtcVideoBufferMultiFormat> FEpicRtcVideoBufferMultiFormatLayered::GetLayer(FIntPoint TargetResolution) const
	{
		return new FEpicRtcVideoBufferMultiFormat(FrameCapturer, TargetResolution);
	}

	FEpicRtcVideoBufferMultiFormat::FEpicRtcVideoBufferMultiFormat(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer, FIntPoint TargetResolution)
		: FEpicRtcVideoBufferMultiFormatBase(InFrameCapturer)
		, Resolution(TargetResolution)
	{
	}

	int FEpicRtcVideoBufferMultiFormat::GetWidth()
	{
		return Resolution.X;
	}

	int FEpicRtcVideoBufferMultiFormat::GetHeight()
	{
		return Resolution.Y;
	}

	IPixelCaptureOutputFrame* FEpicRtcVideoBufferMultiFormat::RequestFormat(int32 Format) const
	{
		// ensure this frame buffer will always refer to the same frame
		if (TSharedPtr<IPixelCaptureOutputFrame>* CachedFrame = CachedFormat.Find(Format))
		{
			return CachedFrame->Get();
		}

		if (!FrameCapturer)
		{
			return nullptr;
		}
		TSharedPtr<IPixelCaptureOutputFrame> Frame = FrameCapturer->RequestFormat(Format, Resolution);
		if (!Frame) 
		{
			return nullptr;
		}
		
		CachedFormat.Add(Format, Frame);
		return Frame.Get();
	}
} // namespace UE::PixelStreaming2