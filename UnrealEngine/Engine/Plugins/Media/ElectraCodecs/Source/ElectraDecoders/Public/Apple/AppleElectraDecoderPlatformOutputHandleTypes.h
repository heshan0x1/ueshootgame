// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "IElectraTextureSampleEncoding.h"

enum class EElectraDecoderPlatformOutputHandleType
{
	ImageBuffers			// IElectraDecoderVideoOutputImageBuffers interface
};


class FElectraDecoderOutputSync final
{
public:
	FElectraDecoderOutputSync() = default;
	~FElectraDecoderOutputSync() = default;
};

class IElectraDecoderVideoOutputImageBuffers
{
public:
	// Return the 4cc of the codec. This determines how the rest of the information must be interpreted.
	virtual uint32 GetCodec4CC() const = 0;

	// Returns the number of separate image buffers making up the frame.
	virtual int32 GetNumberOfBuffers() const = 0;

	// Returns the n'th image buffer data address.
	virtual TSharedPtr<TArray64<uint8>, ESPMode::ThreadSafe> GetBufferDataByIndex(int32 InBufferIndex) const = 0;

	// Returns the n'th CFImageBuffer ("texture") reference.
	virtual void* GetBufferTextureByIndex(int32 InBufferIndex) const = 0;

	// Returns the n'th image buffer's GPU sync object to signal texture data as being ready to be read
	virtual bool GetBufferTextureSyncByIndex(int32 InBufferIndex, FElectraDecoderOutputSync& SyncObject) const = 0;

	// Returns the n'th image buffer format
	virtual EPixelFormat GetBufferFormatByIndex(int32 InBufferIndex) const = 0;

	// Returns the n'th image buffer encoding
	virtual EElectraTextureSamplePixelEncoding GetBufferEncodingByIndex(int32 InBufferIndex) const = 0;

	// Returns the n'th image buffer pitch
	virtual int32 GetBufferPitchByIndex(int32 InBufferIndex) const = 0;
};
