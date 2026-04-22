// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaTextureSample.h"

#include "NDIMediaAPI.h"
#include "NDIMediaTextureSampleConverter.h"

bool FNDIMediaTextureSample::Initialize(const NDIlib_video_frame_v2_t& InVideoFrame, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs, const FTimespan& InTime, const TOptional<FTimecode>& InTimecode)
{
	bIsCustomFormat = false;
	bIsProgressive = true;
	FieldIndex = 0;
	
	int32 FrameBufferSize = 0;
	EMediaTextureSampleFormat FrameSampleFormat;
	bool bNeedConverter = false;

	if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_UYVY)
	{
		FrameBufferSize = InVideoFrame.line_stride_in_bytes * InVideoFrame.yres;
		FrameSampleFormat = EMediaTextureSampleFormat::CharUYVY;
	}
	else if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_BGRA)
	{
		FrameBufferSize = InVideoFrame.line_stride_in_bytes * InVideoFrame.yres;
		FrameSampleFormat = EMediaTextureSampleFormat::CharBGRA;
	}
	else if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_RGBA
		|| InVideoFrame.FourCC == NDIlib_FourCC_video_type_RGBX)
	{
		FrameBufferSize = InVideoFrame.line_stride_in_bytes * InVideoFrame.yres;
		FrameSampleFormat = EMediaTextureSampleFormat::CharRGBA;
	}
	else if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_UYVA)
	{
		// UYVA format needs a custom converter.
		bNeedConverter = true;
		FrameBufferSize = InVideoFrame.line_stride_in_bytes * InVideoFrame.yres + InVideoFrame.xres*InVideoFrame.yres;
		FrameSampleFormat = EMediaTextureSampleFormat::CharRGBA; // Resulting texture needs to be RGBA.
	}
	else
	{
		return false;
	}

	// Custom converter is recycled. We keep a backup to avoid it being reset by FreeSample (called by the base class's Initialize).
	TSharedPtr<FNDIMediaTextureSampleConverter> ConverterBackup = CustomConverter;

	bool bInitSuccess = false;

	switch (InVideoFrame.frame_format_type)
	{
		case NDIlib_frame_format_type_progressive:
			bInitSuccess = Super::Initialize(InVideoFrame.p_data
				, FrameBufferSize
				, InVideoFrame.line_stride_in_bytes
				, InVideoFrame.xres
				, InVideoFrame.yres
				, FrameSampleFormat
				, InTime
				, FFrameRate(InVideoFrame.frame_rate_N, InVideoFrame.frame_rate_D)
				, InTimecode
				, InColorFormatArgs);
			bIsProgressive = true;
			break;

		case NDIlib_frame_format_type_field_0:
		case NDIlib_frame_format_type_field_1:
			bInitSuccess = Super::InitializeWithEvenOddLine(InVideoFrame.frame_format_type == NDIlib_frame_format_type_field_0
			, InVideoFrame.p_data
			, FrameBufferSize
			, InVideoFrame.line_stride_in_bytes
			, InVideoFrame.xres
			, InVideoFrame.yres
			, FrameSampleFormat
			, InTime
			, FFrameRate(InVideoFrame.frame_rate_N, InVideoFrame.frame_rate_D)
			, InTimecode
			, InColorFormatArgs);
			bIsProgressive = false;
			FieldIndex = InVideoFrame.frame_format_type == NDIlib_frame_format_type_field_0 ? 0 : 1;
			break;
		default:
			break;
	}

	if (bInitSuccess && bNeedConverter)
	{
		bIsCustomFormat = bNeedConverter;

		// Either recycle or allocate a new custom sample converter.
		CustomConverter = ConverterBackup.IsValid() ? ConverterBackup : MakeShared<FNDIMediaTextureSampleConverter>();
	}

	return bInitSuccess;
}

IMediaTextureSampleConverter* FNDIMediaTextureSample::GetMediaTextureSampleConverter()
{
	if (bIsCustomFormat)
	{
		return CustomConverter.Get();
	}
	return Super::GetMediaTextureSampleConverter();
}

void FNDIMediaTextureSample::CopyConfiguration(const TSharedPtr<FMediaIOCoreTextureSampleBase>& SourceSample)
{
	if (FNDIMediaTextureSample* SourceSampleNdi = static_cast<FNDIMediaTextureSample*>(SourceSample.Get()))
	{
		bIsCustomFormat = SourceSampleNdi->bIsCustomFormat;
		bIsProgressive = SourceSampleNdi->bIsProgressive;
		FieldIndex = SourceSampleNdi->FieldIndex;
		CustomConverter = SourceSampleNdi->CustomConverter;
	}

	// We need to preserve the JITR converter so we don't lose it.
	TSharedPtr<FMediaIOCoreTextureSampleConverter> ProxyConverterBackup = Converter;

	// NDI source samples don't have a JITR converter and will reset the JITR proxy sample's.
	FMediaIOCoreTextureSampleBase::CopyConfiguration(SourceSample);

	// Restore the JITR converter.
	Converter = ProxyConverterBackup;
}

void FNDIMediaTextureSample::FreeSample()
{
	bIsCustomFormat = false;
	CustomConverter.Reset();
	FMediaIOCoreTextureSampleBase::FreeSample();
}