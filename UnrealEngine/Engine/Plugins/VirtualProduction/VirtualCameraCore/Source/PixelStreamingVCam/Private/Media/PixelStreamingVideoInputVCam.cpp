// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInputVCam.h"
#include "PixelStreamingSettings.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureCapturerRHIRDG.h"
#include "PixelCaptureCapturerRHINoCopy.h"
#include "PixelCaptureCapturerRHIToI420CPU.h"
#include "PixelCaptureCapturerRHIToI420Compute.h"
#include "RHI.h"

TSharedPtr<FPixelStreamingVideoInputVCam> FPixelStreamingVideoInputVCam::Create()
{
	TSharedPtr<FPixelStreamingVideoInputVCam> NewInput = TSharedPtr<FPixelStreamingVideoInputVCam>(new FPixelStreamingVideoInputVCam());
	return NewInput;
}

FPixelStreamingVideoInputVCam::FPixelStreamingVideoInputVCam()
{
}

FPixelStreamingVideoInputVCam::~FPixelStreamingVideoInputVCam()
{
}

FString FPixelStreamingVideoInputVCam::ToString()
{
	return TEXT("a Virtual Camera");
}

TSharedPtr<FPixelCaptureCapturer> FPixelStreamingVideoInputVCam::CreateCapturer(int32 FinalFormat, FIntPoint OutputResolution)
{
	switch (FinalFormat)
	{
		case PixelCaptureBufferFormat::FORMAT_RHI:
		{
			if (FPixelStreamingSettings::GetSimulcastParameters().Layers.Num() == 1 && FPixelStreamingSettings::GetSimulcastParameters().Layers[0].Scaling == 1.0)
			{
				// If we only have a single layer (and it's scale is 1), we can use the no copy capturer
				// as we know the output from the media capture will already be the correct format and scale
				return FPixelCaptureCapturerRHINoCopy::Create();
			}
			else
			{
				// "Safe Texture Copy" polls a fence to ensure a GPU copy is complete
				// the RDG pathway does not poll a fence so is more unsafe but offers
				// a significant performance increase
				if (FPixelStreamingSettings::GetCaptureUseFence())
				{
					return FPixelCaptureCapturerRHI::Create({ .OutputResolution = OutputResolution });
				}
				else
				{
					return FPixelCaptureCapturerRHIRDG::Create({ .OutputResolution = OutputResolution });
				}
			}
		}
		case PixelCaptureBufferFormat::FORMAT_I420:
		{
			if (FPixelStreamingSettings::GetVPXUseCompute())
			{
				return FPixelCaptureCapturerRHIToI420Compute::Create({ .OutputResolution = OutputResolution });
			}
			else
			{
				return FPixelCaptureCapturerRHIToI420CPU::Create({ .OutputResolution = OutputResolution });
			}
		}
		default:
			// UE_LOG(LogPixelStreaming, Error, TEXT("Unsupported final format %d"), FinalFormat);
			return nullptr;
	}
}
