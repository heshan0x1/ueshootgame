// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "VideoCapturer.h"

#define UE_API PIXELSTREAMING2_API

namespace UE::PixelStreaming2
{
	class FVideoSource
	{
	public:
		virtual ~FVideoSource() = default;

		UE_API virtual void PushFrame();
		UE_API virtual void ForceKeyFrame();
		UE_API virtual void SetMuted(bool bIsMuted);

	protected:
		bool bIsMuted = false;
	};
} // namespace UE::PixelStreaming2

#undef UE_API
