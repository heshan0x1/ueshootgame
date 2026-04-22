// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoProducer.h"
#include "Widgets/SWindow.h"

#define UE_API PIXELSTREAMING2_API

namespace UE::PixelStreaming2
{
	/**
	 * Use this if you want to send the contents of the UE backbuffer.
	 */
	class FVideoProducerBackBuffer : public FVideoProducerBase
	{
	public:
		static UE_API TSharedPtr<FVideoProducerBackBuffer> Create();
		UE_API virtual ~FVideoProducerBackBuffer();

		UE_API virtual FString ToString() override;

	private:
		FVideoProducerBackBuffer() = default;

		UE_API void OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& FrameBuffer);

		FDelegateHandle DelegateHandle;
	};

} // namespace UE::PixelStreaming2

#undef UE_API
