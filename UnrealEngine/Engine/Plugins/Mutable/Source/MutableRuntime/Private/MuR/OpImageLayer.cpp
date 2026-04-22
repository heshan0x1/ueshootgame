// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpImageLayer.h"

#include "MuR/ImagePrivate.h"
#include "MuR/ImageRLE.h"
#include "MuR/ParallelExecutionUtils.h"
#include "Templates/UnrealTemplate.h"

#include "MuR/OpImageBlend.h"
#include "MuR/MutableRuntimeModule.h"

namespace UE::Mutable::Private
{

namespace OpImageLayerInternal
{

	struct FOpLayerBatchArgs
	{
		int32 BatchNumElems  = 0;
		int32 LODBegin       = 0;
		int32 LODEnd         = 0;
		int32 FirstLODOffset = 0;

		FImage* Result       = nullptr;
		const FImage* Base   = nullptr;
		const FImage* Blend  = nullptr;
		const FImage* Mask   = nullptr;

		uint32 ResultBytesPerElem = 0;
		uint32 BaseBytesPerElem   = 0;
		uint32 BlendBytesPerElem  = 0;
		uint32 MaskBytesPerElem   = 0;
	};

	struct FOpLayerBatchViews
	{
		int32 NumElems = 0;

		TArrayView<uint8> Result;
		TArrayView<const uint8> Base;
		TArrayView<const uint8> Blend;
		TArrayView<const uint8> Mask;
	};
	
	FORCENOINLINE int32 GetOpLayerNumBatches(const FOpLayerBatchArgs& Args)
	{	
		check(Args.Result);
		const int32 NumBatches = Args.Result->DataStorage.GetNumBatches(Args.BatchNumElems, Args.ResultBytesPerElem);

#if DO_CHECK
		if (Args.Base)
		{
			check(NumBatches == Args.Base->DataStorage.GetNumBatches(Args.BatchNumElems, Args.BaseBytesPerElem));
		}
	
		if (Args.Blend)
		{
			check(NumBatches == Args.Blend->DataStorage.GetNumBatches(Args.BatchNumElems, Args.BlendBytesPerElem));
		}

		if (Args.Mask)
		{
			check(NumBatches == Args.Mask->DataStorage.GetNumBatches(Args.BatchNumElems, Args.MaskBytesPerElem));
		}
#endif
		return NumBatches;
	}

	FORCENOINLINE int32 GetOpLayerNumBatchesLODRange(const FOpLayerBatchArgs& Args)
	{
		check(Args.Result);
		const int32 NumBatches = Args.Result->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.ResultBytesPerElem, Args.LODBegin, Args.LODEnd);

#if DO_CHECK
		if (Args.Base)
		{
			check(NumBatches == Args.Base->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.BaseBytesPerElem, Args.LODBegin, Args.LODEnd));
		}
	
		if (Args.Blend)
		{
			check(NumBatches == Args.Blend->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.BlendBytesPerElem, Args.LODBegin, Args.LODEnd));
		}

		if (Args.Mask)
		{
			check(NumBatches == Args.Mask->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.MaskBytesPerElem, Args.LODBegin, Args.LODEnd));
		}
#endif
		return NumBatches;
	}
	
	FORCENOINLINE int32 GetOpLayerNumBatchesLODRangeOffsetViews(const FOpLayerBatchArgs& Args)
	{
		const bool bOnlyFirstLOD = Args.FirstLODOffset >= 0;

		check(Args.Result);
		const int32 NumBatches = bOnlyFirstLOD
				? Args.Result->DataStorage.GetNumBatchesFirstLODOffset(Args.BatchNumElems, Args.ResultBytesPerElem, Args.FirstLODOffset)
				: Args.Result->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.ResultBytesPerElem, Args.LODBegin, Args.LODEnd);

#if DO_CHECK
		if (Args.Base)
		{
			const int32 BaseNumBatches = bOnlyFirstLOD
					? Args.Base->DataStorage.GetNumBatchesFirstLODOffset(Args.BatchNumElems, Args.BaseBytesPerElem, Args.FirstLODOffset)
					: Args.Base->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.BaseBytesPerElem, Args.LODBegin, Args.LODEnd);

			check(NumBatches == BaseNumBatches);
		}
	
		if (Args.Blend)
		{

			const int32 BlendNumBatches = bOnlyFirstLOD
					? Args.Blend->DataStorage.GetNumBatchesFirstLODOffset(Args.BatchNumElems, Args.BlendBytesPerElem, Args.FirstLODOffset)
					: Args.Blend->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.BlendBytesPerElem, Args.LODBegin, Args.LODEnd);

			check(NumBatches == BlendNumBatches);
		}

		if (Args.Mask)
		{
			const int32 MaskNumBatches = bOnlyFirstLOD
					? Args.Mask->DataStorage.GetNumBatchesFirstLODOffset(Args.BatchNumElems, Args.MaskBytesPerElem, Args.FirstLODOffset)
					: Args.Mask->DataStorage.GetNumBatchesLODRange(Args.BatchNumElems, Args.MaskBytesPerElem, Args.LODBegin, Args.LODEnd);

			check(NumBatches == MaskNumBatches);
		}
#endif
		return NumBatches;
	}


	FORCENOINLINE FOpLayerBatchViews GetOpLayerBatchViews(int32 BatchId, const FOpLayerBatchArgs& Args)
	{
		FOpLayerBatchViews BatchViewsResult;
	
		check(Args.Result);
		BatchViewsResult.Result = Args.Result->DataStorage.GetBatch(BatchId, Args.BatchNumElems, Args.ResultBytesPerElem);

		BatchViewsResult.NumElems = BatchViewsResult.Result.Num() / Args.ResultBytesPerElem;

		if (Args.Base)
		{
			BatchViewsResult.Base = Args.Base->DataStorage.GetBatch(BatchId, Args.BatchNumElems, Args.BaseBytesPerElem);
			check(BatchViewsResult.NumElems == BatchViewsResult.Base.Num() / Args.BaseBytesPerElem);
		}

		if (Args.Blend)
		{
			BatchViewsResult.Blend = Args.Blend->DataStorage.GetBatch(BatchId, Args.BatchNumElems, Args.BlendBytesPerElem);
			check(BatchViewsResult.NumElems == BatchViewsResult.Blend.Num() / Args.BlendBytesPerElem);
		}

		if (Args.Mask)
		{
			BatchViewsResult.Mask = Args.Mask->DataStorage.GetBatch(BatchId, Args.BatchNumElems, Args.MaskBytesPerElem);
			check(BatchViewsResult.NumElems == BatchViewsResult.Mask.Num() / Args.MaskBytesPerElem);
		}

		return BatchViewsResult;
	}

	FORCENOINLINE FOpLayerBatchViews GetOpLayerBatchLODRangeViews(int32 BatchId, const FOpLayerBatchArgs& Args)
	{
		FOpLayerBatchViews BatchViewsResult;
		
		check(Args.Result);
		BatchViewsResult.Result = 
					Args.Result->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.ResultBytesPerElem, Args.LODBegin, Args.LODEnd);

		BatchViewsResult.NumElems = BatchViewsResult.Result.Num() / Args.ResultBytesPerElem;

		if (Args.Base)
		{
			BatchViewsResult.Base = 
					Args.Base->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.BaseBytesPerElem, Args.LODBegin, Args.LODEnd);
			check(BatchViewsResult.NumElems == BatchViewsResult.Base.Num() / Args.BaseBytesPerElem);
		}

		if (Args.Blend)
		{
			BatchViewsResult.Blend = 
					Args.Blend->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.BlendBytesPerElem, Args.LODBegin, Args.LODEnd);
			check(BatchViewsResult.NumElems == BatchViewsResult.Blend.Num() / Args.BlendBytesPerElem);
		}

		if (Args.Mask)
		{
			BatchViewsResult.Mask = 
					Args.Mask->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.MaskBytesPerElem, Args.LODBegin, Args.LODEnd);
			check(BatchViewsResult.NumElems == BatchViewsResult.Mask.Num() / Args.MaskBytesPerElem);
		}

		return BatchViewsResult;
	}

	FORCENOINLINE FOpLayerBatchViews GetOpLayerBatchLODRangeOffsetViews(int32 BatchId, const FOpLayerBatchArgs& Args)
	{
		FOpLayerBatchViews BatchViewsResult;
		
		const bool bOnlyFirstLOD = Args.FirstLODOffset >= 0;

		check(Args.Result);
		BatchViewsResult.Result = bOnlyFirstLOD
				? Args.Result->DataStorage.GetBatchFirstLODOffset(BatchId, Args.BatchNumElems, Args.ResultBytesPerElem, Args.FirstLODOffset) 
				: Args.Result->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.ResultBytesPerElem, Args.LODBegin, Args.LODEnd);

		BatchViewsResult.NumElems = BatchViewsResult.Result.Num() / Args.ResultBytesPerElem;
		
		if (Args.Base)
		{
			BatchViewsResult.Base = bOnlyFirstLOD
					? Args.Base->DataStorage.GetBatchFirstLODOffset(BatchId, Args.BatchNumElems, Args.BaseBytesPerElem, Args.FirstLODOffset) 
					: Args.Base->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.BaseBytesPerElem, Args.LODBegin, Args.LODEnd); 

			check(BatchViewsResult.NumElems == BatchViewsResult.Base.Num() / Args.BaseBytesPerElem);
		}

		if (Args.Blend)
		{
			BatchViewsResult.Blend = bOnlyFirstLOD
					? Args.Blend->DataStorage.GetBatchFirstLODOffset(BatchId, Args.BatchNumElems, Args.BlendBytesPerElem, Args.FirstLODOffset) 
					: Args.Blend->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.BlendBytesPerElem, Args.LODBegin, Args.LODEnd); 

			check(BatchViewsResult.NumElems == BatchViewsResult.Blend.Num() / Args.BlendBytesPerElem);
		}

		if (Args.Mask)
		{
			BatchViewsResult.Mask = bOnlyFirstLOD
					? Args.Mask->DataStorage.GetBatchFirstLODOffset(BatchId, Args.BatchNumElems, Args.MaskBytesPerElem, Args.FirstLODOffset) 
					: Args.Mask->DataStorage.GetBatchLODRange(BatchId, Args.BatchNumElems, Args.MaskBytesPerElem, Args.LODBegin, Args.LODEnd); 

			check(BatchViewsResult.NumElems == BatchViewsResult.Mask.Num() / Args.MaskBytesPerElem);
		}

		return BatchViewsResult;
	}

	FORCEINLINE bool IsAnyComponentLargerThan1(FVector4f Value)
	{
		return (Value[0] > 1) | (Value[1] > 1) | (Value[2] > 1) | (Value[3] > 1);
	}

	/** 
	 * Apply a blending function to an image with a colour source.
	 * It only affects the RGB or L channels, leaving alpha untouched.
	 */
	template<uint32 NumChannels, uint32 (*BLEND_FUNC)(uint32, uint32), bool bClamp>
	FORCENOINLINE void BufferLayerColourGenericChannel(uint8* DestBuf, const uint8* BaseBuf, int32 NumElems, const FIntVector& Color)
	{
		static_assert(NumChannels > 0 && NumChannels <= 4);

		for (int32 I = 0; I < NumElems; ++I)
		{
			for (uint32 C = 0; C < NumChannels; ++C)
			{
				uint32 Base = BaseBuf[NumChannels * I + C];
				uint32 Result = BLEND_FUNC(Base, Color[C]);
				if constexpr (bClamp)
				{
					DestBuf[NumChannels * I + C] = (uint8)FMath::Min(255u, Result);
				}
				else
				{
					DestBuf[NumChannels * I + C] = (uint8)Result;
				}
			}
		}
	}

	template<uint32 NumChannels, uint32 (*BLEND_FUNC)(uint32,uint32), bool Clamp>
	FORCENOINLINE void BufferLayerColourFromAlphaGenericChannel(uint8* DestBuf, const uint8* BaseBuf, int32 NumElems, const FIntVector& Color)
	{
		static_assert(NumChannels > 0 && NumChannels <= 4);

		for (int32 I = 0; I < NumElems; ++I)
		{
			const uint32 Alpha = Invoke([&]() -> uint32
			{
				if constexpr (NumChannels <= 3)
				{
					return 255;
				}
				else
				{
					return BaseBuf[NumChannels * I + 3];
				}
			});

			for (uint32 C = 0; C < NumChannels; ++C)
			{
				uint32 Result = BLEND_FUNC(Alpha, Color[C]);
				if constexpr (Clamp)
				{
					DestBuf[NumChannels * I + C] = (uint8)FMath::Min(255u, Result);
				}
				else
				{
					DestBuf[NumChannels * I + C] = (uint8)Result;
				}
			}
		}
	}
} // namespace OpImageLayerInternal

	template< uint32 (*BLEND_FUNC)(uint32,uint32), bool CLAMP >
    FORCENOINLINE void BufferLayerColourImpl(FImage* ResultImage, const FImage* BaseImage, FVector4f Color)
	{
        check(ResultImage->GetFormat() == BaseImage->GetFormat());
        check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
        check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
        check(ResultImage->GetLODCount() == BaseImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();
		// Generic implementation
		const int32 BytesPerElem = GetImageFormatData(BaseFormat).BytesPerBlock;	

		constexpr int32 BatchNumElems = 4096*4;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.BaseBytesPerElem   = BytesPerElem;
		BatchArgs.ResultBytesPerElem = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatches(BatchArgs);

		const FIntVector ColorValue = FIntVector(Color.X * 255.0f, Color.Y * 255.0f, Color.Z * 255.0f);
		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, ColorValue, BaseFormat
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchViews(BatchId, BatchArgs);
	
			switch (BaseFormat)
			{
			case EImageFormat::L_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 1);
				OpImageLayerInternal::BufferLayerColourGenericChannel<1, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::RGB_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 3);
				OpImageLayerInternal::BufferLayerColourGenericChannel<3, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::RGBA_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 4);
				OpImageLayerInternal::BufferLayerColourGenericChannel<4, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::BGRA_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 4);
				FIntVector BGRAColorValue = FIntVector(ColorValue.Z, ColorValue.Y, ColorValue.X);
				OpImageLayerInternal::BufferLayerColourGenericChannel<4, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			default:
			{
				checkf(false, TEXT("Unsupported format."));
				break;
			}
			}
		});
    }


	template< uint32 (*BLEND_FUNC)(uint32, uint32) >
	void BufferLayerColour(FImage* ResultImage, const FImage* BaseImage, FVector4f Color)
	{
		bool bIsClampNeeded = OpImageLayerInternal::IsAnyComponentLargerThan1(Color);
		if (bIsClampNeeded)
		{
			BufferLayerColourImpl<BLEND_FUNC, true>(ResultImage, BaseImage, Color);
		}
		else
		{
			BufferLayerColourImpl<BLEND_FUNC, false>(ResultImage, BaseImage, Color);
		}
	}

	template<
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		bool CLAMP>
    FORCENOINLINE void BufferLayerColourFromAlpha(FImage* ResultImage, const FImage* BaseImage, FVector4f Color)
	{
        check(ResultImage->GetFormat() == BaseImage->GetFormat());
        check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
        check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
        check(ResultImage->GetLODCount() == BaseImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();
		// Generic implementation
		constexpr int32 BatchNumElems = 4096*2;
		const int32 BytesPerElem = GetImageFormatData(BaseFormat).BytesPerBlock;	

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.BaseBytesPerElem   = BytesPerElem;
		BatchArgs.ResultBytesPerElem = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatches(BatchArgs);

		const FIntVector ColorValue = FIntVector(Color.X * 255.0f, Color.Y * 255.0f, Color.Z * 255.0f);
		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, ColorValue, BaseFormat
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchViews(BatchId, BatchArgs);
	
			switch (BaseFormat)
			{
			case EImageFormat::L_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 1);
				OpImageLayerInternal::BufferLayerColourFromAlphaGenericChannel<1, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::RGB_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 3);
				OpImageLayerInternal::BufferLayerColourFromAlphaGenericChannel<3, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::RGBA_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 4);
				OpImageLayerInternal::BufferLayerColourFromAlphaGenericChannel<4, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			case EImageFormat::BGRA_UByte:
			{
				check(BatchArgs.ResultBytesPerElem == 4);
				FIntVector BGRAColorValues = FIntVector(ColorValue.Z, ColorValue.Y, ColorValue.X);
				OpImageLayerInternal::BufferLayerColourFromAlphaGenericChannel<4, BLEND_FUNC, CLAMP>(
						BatchViews.Result.GetData(), BatchViews.Base.GetData(), BatchViews.NumElems, ColorValue);
				break;
			}
			default:
			{
				checkf(false, TEXT("Unsupported format."));
				break;
			}
			}
		});
    }


	template<uint32 (*BLEND_FUNC)(uint32, uint32)>
	void BufferLayerColourFromAlpha(FImage* ResultImage, const FImage* BaseImage, FVector4f Color)
	{
		bool bIsClampNeeded = OpImageLayerInternal::IsAnyComponentLargerThan1(Color);
		if (bIsClampNeeded)
		{
			BufferLayerColourFromAlpha<BLEND_FUNC, true>(ResultImage, BaseImage, Color);
		}
		else
		{
			BufferLayerColourFromAlpha<BLEND_FUNC, false>(ResultImage, BaseImage, Color);
		}
	}


	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32),
		bool CLAMP,
		// Number of total channels to actually process
		uint32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE>
    void BufferLayerColourFormat(FImage* DestImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Col, uint32 BaseChannelOffset, uint8 ColorChannelOffset, bool bOnlyFirstLOD)
	{
		check(CHANNELS_TO_BLEND + BaseChannelOffset <= BASE_CHANNEL_STRIDE);
		check(DestImage->GetLODCount() <= BaseImage->GetLODCount());

		FUintVector4 TopColor = FUintVector4(Col.X * 255.0f, Col.Y * 255.0f, Col.Z * 255.0f, Col.W * 255);

		const EImageFormat BaseFormat = BaseImage->GetFormat();
		const EImageFormat MaskFormat = MaskImage->GetFormat();

		int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

        const bool bIsMaskUncompressed = (MaskFormat == EImageFormat::L_UByte);

		constexpr uint32 NumColorChannels = FMath::Min(CHANNELS_TO_BLEND, 3u);
		check(NumColorChannels + ColorChannelOffset < 4);

        if (bIsMaskUncompressed)
        {
			const int32 BytesPerElem = GetImageFormatData(BaseFormat).BytesPerBlock;
			check(GetImageFormatData(MaskFormat).BytesPerBlock == 1);
			check(GetImageFormatData(DestImage->GetFormat()).BytesPerBlock == BytesPerElem);

			constexpr int32 BatchNumElems = 4096 * 2;
			OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
			BatchArgs.BatchNumElems      = BatchNumElems; 
			BatchArgs.LODBegin           = 0;
			BatchArgs.LODEnd             = NumLODs;
			BatchArgs.Result             = DestImage;
			BatchArgs.Base               = BaseImage;
			BatchArgs.Mask               = MaskImage;
			BatchArgs.ResultBytesPerElem = BytesPerElem;
			BatchArgs.BaseBytesPerElem   = BytesPerElem;
			BatchArgs.MaskBytesPerElem   = 1;

			const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

			ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
			[
				&BatchArgs, BaseChannelOffset, TopColor, ColorChannelOffset, NumColorChannels
			](int32 BatchId)
			{
				OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
						OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

				// This could happen in case of missing data files.
				if (!BatchViews.Base.GetData() || !BatchViews.Mask.GetData() || !BatchViews.Result.GetData())
				{
					return;
				}

				const uint8* BaseBuf = BatchViews.Base.GetData() + BaseChannelOffset;
				const uint8* MaskBuf = BatchViews.Mask.GetData();
				uint8* DestBuf       = BatchViews.Result.GetData() + BaseChannelOffset;

				for (int32 I = 0; I < BatchViews.NumElems; ++I)
				{
					uint32 MaskData = MaskBuf[I];
					for (int32 C = 0; C < NumColorChannels; ++C)
					{
						const uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE*I + C];
						const uint32 Result = BLEND_FUNC_MASKED(Base, TopColor[C + ColorChannelOffset], MaskData);
						if constexpr (CLAMP)
						{
							DestBuf[BASE_CHANNEL_STRIDE*I + C] = (uint8)FMath::Min(255u, Result);
						}
						else
						{
							DestBuf[BASE_CHANNEL_STRIDE*I + C] = (uint8)Result;
						}
					}

					constexpr bool bIsNC4 = (BASE_CHANNEL_STRIDE == 4);
					if constexpr (bIsNC4)
					{
						DestBuf[BASE_CHANNEL_STRIDE*I + 3] = BaseBuf[BASE_CHANNEL_STRIDE*I + 3];
					}
				}
			});
        }
        else if (MaskFormat == EImageFormat::L_UByteRLE)
        {
            int32 Rows = BaseImage->GetSizeY();
            int32 Width = BaseImage->GetSizeX();

            for (int32 Lod = 0; Lod < NumLODs; ++Lod)
            {
                const uint8* BaseBuf = BaseImage->GetLODData(Lod);
                const uint8* MaskBuf = MaskImage->GetLODData(Lod);
				uint8* DestBuf = DestImage->GetLODData(Lod);

				// This could happen in case of missing data files.
				if (!BaseBuf || !MaskBuf || !DestBuf)
				{
					continue;
				}

				// Remove RLE header, mip size and row sizes.
				MaskBuf += sizeof(uint32);
				MaskBuf += Rows * sizeof(uint32);

                for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
                {
                    const uint8* DestRowEnd = DestBuf + Width * BASE_CHANNEL_STRIDE;
                    while (DestBuf != DestRowEnd)
                    {
                        // Decode header
						uint16 Equal = 0;
						FMemory::Memmove(&Equal, MaskBuf, sizeof(uint16));
                        MaskBuf += 2;

                        uint8 Different = *MaskBuf;
                        ++MaskBuf;

                        uint8 EqualPixel = *MaskBuf;
                        ++MaskBuf;

                        // Equal pixels
						//check(DestBuf + BASE_CHANNEL_STRIDE * Equal <= BaseImage->GetDataSize(Lod));
                        if (EqualPixel == 255)
                        {
                            for (int32 I = 0; I < Equal; ++I)
                            {
                                for (int32 C = 0; C < NumColorChannels; ++C)
                                {
                                    uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
                                    uint32 Result = BLEND_FUNC(Base, TopColor[C + ColorChannelOffset]);
                                    if constexpr (CLAMP)
                                    {
                                        DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
                                    }
                                    else
                                    {
                                        DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
                                    }
                                }

                                constexpr bool bIsNC4 = (BASE_CHANNEL_STRIDE == 4);
                                if (bIsNC4)
                                {
                                    DestBuf[BASE_CHANNEL_STRIDE * I + 3] = BaseBuf[BASE_CHANNEL_STRIDE * I + 3];
                                }
                            }
                        }
                        else if (EqualPixel > 0)
                        {
                            for (int32 I = 0; I < Equal; ++I)
                            {
                                for (int32 C = 0; C < NumColorChannels; ++C)
                                {
                                    uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
                                    uint32 Result = BLEND_FUNC_MASKED(Base, TopColor[C + ColorChannelOffset], EqualPixel);
                                    if constexpr (CLAMP)
                                    {
                                        DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
                                    }
                                    else
                                    {
                                        DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
                                    }
                                }

                                constexpr bool bIsNC4 = (BASE_CHANNEL_STRIDE == 4);
                                if (bIsNC4)
                                {
                                    DestBuf[BASE_CHANNEL_STRIDE * I + 3] = BaseBuf[BASE_CHANNEL_STRIDE * I + 3];
                                }
                            }
                        }
                        else
                        {
                            // It could happen if xxxxxOnBase
                            if (DestBuf != BaseBuf)
                            {
                                FMemory::Memmove(DestBuf, BaseBuf, BASE_CHANNEL_STRIDE*Equal);
                            }
                        }

                        DestBuf += BASE_CHANNEL_STRIDE * Equal;
                        BaseBuf += BASE_CHANNEL_STRIDE * Equal;

                        // Different pixels
						//check(DestBuf + BASE_CHANNEL_STRIDE * Different <= StartDestBuf + BaseImage->GetDataSize(Lod));
                        for (int32 I = 0; I < Different; ++I)
                        {
                            for (int32 C = 0; C < NumColorChannels; ++C)
                            {
                                uint32 Mask = MaskBuf[I];
                                uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
                                uint32 Result = BLEND_FUNC_MASKED(Base, TopColor[C + ColorChannelOffset], Mask);
                                if constexpr (CLAMP)
                                {
                                    DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
                                }
                                else
                                {
                                    DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
                                }
                            }

                            constexpr bool bIsNC4 = (BASE_CHANNEL_STRIDE == 4);
                            if (bIsNC4)
                            {
                                DestBuf[BASE_CHANNEL_STRIDE * I + 3] = BaseBuf[BASE_CHANNEL_STRIDE * I + 3];
                            }
                        }

                        DestBuf += BASE_CHANNEL_STRIDE * Different;
                        BaseBuf += BASE_CHANNEL_STRIDE * Different;
                        MaskBuf += Different;
                    }
                }

                Rows = FMath::DivideAndRoundUp(Rows, 2);
                Width = FMath::DivideAndRoundUp(Width, 2);
            }
        }
        else
        {
            checkf( false, TEXT("Unsupported mask format.") );
        }
	}


	/**
	* Apply a blending function to an image with a colour source and a mask
	*/
	template<
		uint32 (*BLEND_FUNC_MASKED)(uint32,uint32,uint32),
		uint32 (*BLEND_FUNC)(uint32,uint32),
		bool CLAMP>
    FORCENOINLINE void BufferLayerColourImpl(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Col)
	{
		check(BaseImage->GetSizeX() == MaskImage->GetSizeX());
		check(BaseImage->GetSizeY() == MaskImage->GetSizeY());
		check(MaskImage->GetFormat() == EImageFormat::L_UByte ||
			  MaskImage->GetFormat() == EImageFormat::L_UByteRLE);

		const bool bValid = (BaseImage->GetSizeX() == MaskImage->GetSizeX()) &&
						    (BaseImage->GetSizeY() == MaskImage->GetSizeY()) &&
							(MaskImage->GetFormat() == EImageFormat::L_UByte || MaskImage->GetFormat() == EImageFormat::L_UByteRLE);
		if (!bValid)
		{
			return;
		}

		EImageFormat BaseFormat = BaseImage->GetFormat();
		if (BaseFormat == EImageFormat::RGB_UByte)
		{
			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 3>(ResultImage, BaseImage, MaskImage, Col, 0, 0, false);
		}
        else if (BaseFormat == EImageFormat::RGBA_UByte)
        {
            BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 4>(ResultImage, BaseImage, MaskImage, Col, 0, 0, false);
        }
        else if (BaseFormat == EImageFormat::BGRA_UByte)
        {
            float Temp = Col[0];
            Col[0] = Col[2];
            Col[2] = Temp;
            BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 4>(ResultImage, BaseImage, MaskImage, Col, 0, 0, false);
        }
        else if (BaseFormat == EImageFormat::L_UByte)
		{
			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 1, 1>(ResultImage, BaseImage, MaskImage, Col, 0, 0, false);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}


	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32)>
	void BufferLayerColour(FImage* DestImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Col)
	{
		bool bIsClampNeeded = OpImageLayerInternal::IsAnyComponentLargerThan1(Col);
		if (bIsClampNeeded)
		{
			BufferLayerColourImpl<BLEND_FUNC_MASKED, BLEND_FUNC, true>(DestImage, BaseImage, MaskImage, Col);
		}
		else
		{
			BufferLayerColourImpl<BLEND_FUNC_MASKED, BLEND_FUNC, false>(DestImage, BaseImage, MaskImage, Col);
		}
	}


	/**
	* Apply a blending function to an image with another image as blending layer
	*/
	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		bool CLAMP,
		// Number of total channels to actually process
		uint32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE>
	void BufferLayerColourFormatInPlace(FImage* BaseImage, FVector4f Color, uint32 BaseChannelOffset, uint8 ColorChannelOffset, bool bOnlyFirstLOD)
	{
		FUintVector4 TopColor = FUintVector4(Color.X * 255.0f, Color.Y * 255.0f, Color.Z * 255.0f, Color.W * 255.0f);

		int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		const int32 BytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock;
		
		constexpr int32 BatchNumElems = 4096*2;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = BaseImage;
		BatchArgs.ResultBytesPerElem = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, TopColor, BaseChannelOffset, ColorChannelOffset
		] (uint32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);
			
			uint8* BaseBuf = BatchViews.Result.GetData() + BaseChannelOffset;
			
			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
				{
					uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE*I + C];
					uint32 Blended = TopColor[C + ColorChannelOffset];
					uint32 Result = BLEND_FUNC(Base, Blended);
					if constexpr (CLAMP)
					{
						BaseBuf[BASE_CHANNEL_STRIDE*I + C] = (uint8)FMath::Min(255u, Result);
					}
					else
					{
						BaseBuf[BASE_CHANNEL_STRIDE*I + C] = (uint8)Result;
					}
				}
			}
		});
	}


	template<
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		bool CLAMP, 
		uint32 CHANNEL_COUNT>
	FORCENOINLINE void BufferLayerColourInPlaceImpl(FImage* BaseImage, FVector4f Col, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColOffset)
	{
		EImageFormat BaseFormat = BaseImage->GetFormat();

		if (BaseFormat == EImageFormat::RGB_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 3);
			BufferLayerColourFormatInPlace<BLEND_FUNC, CLAMP, CHANNEL_COUNT, 3>
					(BaseImage, Col, BaseOffset, ColOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 4);
			BufferLayerColourFormatInPlace<BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4>
					(BaseImage, Col, BaseOffset, ColOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::BGRA_UByte)
		{
			float Temp = Col[0];
			Col[0] = Col[2];
			Col[2] = Temp;
			BufferLayerColourFormatInPlace<BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4>
					(BaseImage, Col, BaseOffset, ColOffset, bOnlyOneMip);
		}

		else if (BaseFormat == EImageFormat::L_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 1);
			BufferLayerColourFormatInPlace< BLEND_FUNC, CLAMP, CHANNEL_COUNT, 1>
					(BaseImage, Col, BaseOffset, ColOffset, bOnlyOneMip);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}


	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		uint32 CHANNEL_COUNT>
	void BufferLayerColourInPlace(FImage* BaseImage, FVector4f Color, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColorOffset)
	{
		bool bIsClampNeeded = OpImageLayerInternal::IsAnyComponentLargerThan1(Color);
		if (bIsClampNeeded)
		{
			BufferLayerColourInPlaceImpl<BLEND_FUNC, true, CHANNEL_COUNT>(BaseImage, Color, bOnlyOneMip, BaseOffset, ColorOffset);
		}
		else
		{
			BufferLayerColourInPlaceImpl<BLEND_FUNC, false, CHANNEL_COUNT>(BaseImage, Color, bOnlyOneMip, BaseOffset, ColorOffset);
		}
	}


	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		bool CLAMP, 
		int32 CHANNEL_COUNT>
	FORCENOINLINE void BufferLayerColourInPlaceImpl(FImage* BaseImage, const FImage* MaskImage, FVector4f Color, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColorOffset)
	{
		check(BaseImage->GetSizeX() == MaskImage->GetSizeX());
		check(BaseImage->GetSizeY() == MaskImage->GetSizeY());

		EImageFormat BaseFormat = BaseImage->GetFormat();

		if (BaseFormat == EImageFormat::RGB_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 3);
			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, CHANNEL_COUNT, 3>
					(BaseImage, BaseImage, MaskImage, Color, BaseOffset, ColorOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 4);
			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4>
					(BaseImage, BaseImage, MaskImage, Color, BaseOffset, ColorOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::BGRA_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 4);
			float Temp = Color[0];
			Color[0] = Color[2];
			Color[2] = Temp;

			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4>
					(BaseImage, BaseImage, MaskImage, Color, BaseOffset, ColorOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::L_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 1);
			BufferLayerColourFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, CHANNEL_COUNT, 1>
					(BaseImage, BaseImage, MaskImage, Color, BaseOffset, ColorOffset, bOnlyOneMip);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}

	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32), 
		uint32 (*BLEND_FUNC)(uint32, uint32), uint32 CHANNEL_COUNT>
	void BufferLayerColourInPlace(FImage* BaseImage, const FImage* MaskImage, FVector4f Color, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColorOffset)
	{
		bool bIsClampNeeded = OpImageLayerInternal::IsAnyComponentLargerThan1(Color);
		if (bIsClampNeeded)
		{
			BufferLayerColourInPlaceImpl<BLEND_FUNC_MASKED, BLEND_FUNC, true, CHANNEL_COUNT>
					(BaseImage, MaskImage, Color, bOnlyOneMip, BaseOffset, ColorOffset);
		}
		else
		{
			BufferLayerColourInPlaceImpl<BLEND_FUNC_MASKED, BLEND_FUNC, false, CHANNEL_COUNT>
					(BaseImage, MaskImage, Color, bOnlyOneMip, BaseOffset, ColorOffset);
		}
	}


	/**	
	* Apply a blending function to an image with another image as blending layer
	*/
	template< uint32 (*BLEND_FUNC)(uint32, uint32), bool CLAMP,
		// Number of total channels to actually process
		int32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE,
		// Number of total channels in the blend image
		int32 BLENDED_CHANNEL_STRIDE>
    void BufferLayerFormatInPlace(
		FImage* BaseImage, 
		const FImage* BlendedImage,
		uint32 BaseChannelOffset,
		uint32 BlendedChannelOffset,
		bool bOnlyFirstLOD)
	{
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());

		// No longer required.
		//check(BaseImage->GetFormat() == BlendedImage->GetFormat());
		check(BaseChannelOffset + CHANNELS_TO_BLEND <= GetImageFormatData(BaseImage->GetFormat()).Channels);
		check(BlendedChannelOffset + CHANNELS_TO_BLEND <= GetImageFormatData(BlendedImage->GetFormat()).Channels);

		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendedImage->GetLODCount());

		const int32 BaseBytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock;
		const int32 BlendBytesPerElem = GetImageFormatData(BlendedImage->GetFormat()).BytesPerBlock;
	
		const int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		constexpr int32 BatchNumElems = 4096*2;

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = BaseImage;
		BatchArgs.Blend              = BlendedImage;
		BatchArgs.ResultBytesPerElem = BaseBytesPerElem;
		BatchArgs.BlendBytesPerElem  = BlendBytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, BaseChannelOffset, BlendedChannelOffset
		] (int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

        	uint8* BaseBuf = BatchViews.Result.GetData() + BaseChannelOffset;
        	const uint8* BlendedBuf = BatchViews.Blend.GetData() + BlendedChannelOffset;

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
				{
					uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
					uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
					uint32 Result = BLEND_FUNC(Base, Blended);
					
					if constexpr (CLAMP)
					{
						BaseBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
					}
					else
					{
						BaseBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
					}
				}
			}
		});
	}
	
	/**
	* Apply a blending function to an image with another image as blending layer
	*/
	template<
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		bool CLAMP,
		// Number of total channels to actually process
		int32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE,
		// Number of total channels in the blend image
		int32 BLENDED_CHANNEL_STRIDE,
		int32 BLENDED_CHANNEL_OFFSET>
	void BufferLayerFormat(FImage* DestImage, const FImage* BaseImage, const FImage* BlendedImage, bool bOnlyFirstLOD)
	{
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		// Not true anymore, since the BLENDED_CHANNEL_OFFSET has been added.
		// check(BaseImage->GetFormat() == BlendedImage->GetFormat());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendedImage->GetLODCount());

		constexpr int32 UnblendedChannels = BASE_CHANNEL_STRIDE - CHANNELS_TO_BLEND;
		constexpr int32 BatchNumElems = 4096*2;
		
		const int32 BaseBytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock;
		const int32 BlendBytesPerElem = GetImageFormatData(BlendedImage->GetFormat()).BytesPerBlock;
		const int32 DestBytesPerElem = GetImageFormatData(DestImage->GetFormat()).BytesPerBlock;

		const int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = DestImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Blend              = BlendedImage;
		BatchArgs.ResultBytesPerElem = DestBytesPerElem;
		BatchArgs.BaseBytesPerElem   = BaseBytesPerElem;
		BatchArgs.BlendBytesPerElem  = BlendBytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, UnblendedChannels
		] (int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);
	
			const uint8* BaseBuf = BatchViews.Base.GetData();
			const uint8* BlendedBuf = BatchViews.Blend.GetData() + BLENDED_CHANNEL_OFFSET;
			uint8* DestBuf = BatchViews.Result.GetData();

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
				{
					uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
					uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
					uint32 Result = BLEND_FUNC(Base, Blended);

					if constexpr (CLAMP)
					{
						DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
					}
					else
					{
						DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
					}
				}

				// Copy the unblended channels
				// \TODO: unnecessary when doing it in-place?
				if constexpr (UnblendedChannels > 0)
				{
					for (int32 C = 0; C < UnblendedChannels; ++C)
					{
						DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] 
							= BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
					}
				}
			}
		});
	}

	/**
	* Apply a blending function to an image with another image as blending layer
	*/
	template< uint32 (*BLEND_FUNC)(uint32, uint32), bool CLAMP >
	void BufferLayer(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, bool bApplyToAlpha, bool bOnlyOneMip, bool bUseBlendSourceFromBlendAlpha)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(bOnlyOneMip || ResultImage->GetLODCount() == BaseImage->GetLODCount());
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		check(bOnlyOneMip || ResultImage->GetLODCount() <= BlendedImage->GetLODCount());

		EImageFormat BaseFormat = BaseImage->GetFormat();
		EImageFormat BlendedFormat = BlendedImage->GetFormat();

		if (bUseBlendSourceFromBlendAlpha)
		{
			if (BlendedFormat == EImageFormat::RGBA_UByte || BlendedFormat == EImageFormat::BGRA_UByte)
			{
				if (BaseFormat == EImageFormat::L_UByte)
				{
					BufferLayerFormat<BLEND_FUNC, CLAMP, 1, 1, 4, 3>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
				}
				else
				{
					checkf(false, TEXT("Unsupported format."));
				}
			}
			else if (BlendedFormat == EImageFormat::L_UByte)
			{
				BufferLayerFormat<BLEND_FUNC, CLAMP, 1, 1, 1, 0>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
			}
		}
		else
		{
			check(BaseFormat == BlendedFormat);
			if (BaseFormat == EImageFormat::RGB_UByte)
			{
				check(!bUseBlendSourceFromBlendAlpha);
				BufferLayerFormat<BLEND_FUNC, CLAMP, 3, 3, 3, 0>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
			}
			else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
			{
				check(!bUseBlendSourceFromBlendAlpha);
				if (bApplyToAlpha)
				{
					BufferLayerFormat<BLEND_FUNC, CLAMP, 4, 4, 4, 0>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
				}
				else
				{
					BufferLayerFormat<BLEND_FUNC, CLAMP, 3, 4, 4, 0>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
				}
			}
			else if (BaseFormat == EImageFormat::L_UByte)
			{
				BufferLayerFormat<BLEND_FUNC, CLAMP, 1, 1, 1, 0>(ResultImage, BaseImage, BlendedImage, bOnlyOneMip);
			}
			else
			{
				checkf(false, TEXT("Unsupported format."));
			}
		}
	}


	template< uint32 (*BLEND_FUNC)(uint32, uint32), bool CLAMP, int32 CHANNEL_COUNT >
	void BufferLayerInPlace(FImage* BaseImage, const FImage* BlendedImage, bool bOnlyOneMip, uint32 BaseOffset, uint32 BlendedOffset)
	{
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		// Not required since we have the CHANNEL_COUNT and offsets. 
		// check(BaseImage->GetFormat() == BlendedImage->GetFormat());

		EImageFormat BaseFormat = BaseImage->GetFormat();
		EImageFormat BlendFormat = BlendedImage->GetFormat();

		if (BaseFormat == EImageFormat::RGB_UByte && BlendFormat==EImageFormat::RGB_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 3);
			check(BlendedOffset + CHANNEL_COUNT <= 3);
			BufferLayerFormatInPlace<BLEND_FUNC, CLAMP, CHANNEL_COUNT, 3, 3>(BaseImage, BlendedImage, BaseOffset, BlendedOffset, bOnlyOneMip);
		}
		else if ((BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte) &&
				 (BlendFormat == EImageFormat::RGBA_UByte || BlendFormat == EImageFormat::BGRA_UByte) )
		{
			check(BaseOffset + CHANNEL_COUNT <= 4);
			check(BlendedOffset + CHANNEL_COUNT <= 4);
			BufferLayerFormatInPlace< BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4, 4>
					(BaseImage, BlendedImage, BaseOffset, BlendedOffset, bOnlyOneMip);
		}
		else if ((BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte) &&
				 BlendFormat == EImageFormat::L_UByte )
		{
			check(BaseOffset + CHANNEL_COUNT <= 4);
			check(BlendedOffset + CHANNEL_COUNT <= 1);
			BufferLayerFormatInPlace< BLEND_FUNC, CLAMP, CHANNEL_COUNT, 4, 1>
					(BaseImage, BlendedImage, BaseOffset, BlendedOffset, bOnlyOneMip);
		}
		else if (BaseFormat == EImageFormat::L_UByte && BlendFormat==EImageFormat::L_UByte)
		{
			check(BaseOffset + CHANNEL_COUNT <= 1);
			BufferLayerFormatInPlace< BLEND_FUNC, CLAMP, CHANNEL_COUNT, 1, 1>
					(BaseImage, BlendedImage, BaseOffset, BlendedOffset, bOnlyOneMip);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}


	template< uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32),
		bool CLAMP,
		// Number of total channels to actually process
		int32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE,
		// Number of total channels in the blend image
		int32 BLENDED_CHANNEL_STRIDE>
	void BufferLayerFormat(
		FImage* DestImage,
		const FImage* BaseImage,
		const FImage* MaskImage,
		const FImage* BlendImage,
		uint32 DestOffset,
		uint32 BaseChannelOffset,
		uint32 BlendedChannelOffset,
		bool bOnlyFirstLOD)
	{
		check(BaseImage->GetSizeX() == MaskImage->GetSizeX() && BaseImage->GetSizeY() == MaskImage->GetSizeY());
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX() && BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= MaskImage->GetLODCount());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendImage->GetLODCount());
		check(BaseImage->GetFormat() == BlendImage->GetFormat());
		check(MaskImage->GetFormat() == EImageFormat::L_UByte ||
			  MaskImage->GetFormat() == EImageFormat::L_UByteRLE);

		const EImageFormat MaskFormat = MaskImage->GetFormat();

		const int32 BaseBytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock; 
		const int32 BlendBytesPerElem = GetImageFormatData(BlendImage->GetFormat()).BytesPerBlock; 

		const int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		const bool bIsMaskUncompressed = MaskFormat == EImageFormat::L_UByte;
		constexpr int32 UnblendedChannels = BASE_CHANNEL_STRIDE - CHANNELS_TO_BLEND;

		if (bIsMaskUncompressed)
		{	
			constexpr int32 BatchNumElems = 4096*2;

			OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
			BatchArgs.BatchNumElems      = BatchNumElems;
			BatchArgs.LODBegin           = 0;
			BatchArgs.LODEnd             = NumLODs;
			BatchArgs.Result             = DestImage;
			BatchArgs.Base               = BaseImage;
			BatchArgs.Blend              = BlendImage;
			BatchArgs.Mask               = MaskImage;
			BatchArgs.ResultBytesPerElem = BaseBytesPerElem;
			BatchArgs.BaseBytesPerElem   = BaseBytesPerElem;
			BatchArgs.BlendBytesPerElem  = BlendBytesPerElem;
			BatchArgs.MaskBytesPerElem   = 1;

			const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

			ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
			[
				&BatchArgs, UnblendedChannels
			] (uint32 BatchId)
			{
				OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
						OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

				const uint8* BaseBuf = BatchViews.Base.GetData();
				const uint8* BlendedBuf = BatchViews.Blend.GetData();
				const uint8* MaskBuf = BatchViews.Mask.GetData();
				uint8* DestBuf = BatchViews.Result.GetData();

				// This could happen in case of missing data files.
				if (!BaseBuf || !BlendedBuf || !MaskBuf || !DestBuf)
				{
					return;
				}

				for (int32 I = 0; I < BatchViews.NumElems; ++I)
				{
					uint32 Mask = MaskBuf[I];
					for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
					{
						const uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
						const uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
						const uint32 Result = BLEND_FUNC_MASKED(Base, Blended, Mask);
						if constexpr (CLAMP)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
						}
						else
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
						}
					}
					// Copy the unblended channels
					// \TODO: unnecessary when doing it in-place?
					if constexpr (UnblendedChannels > 0)
					{
						for (int32 C = 0; C < UnblendedChannels; ++C)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
						}
					}
				}
			});
		}
		else if (MaskFormat == EImageFormat::L_UByteRLE)
		{
			int32 Rows = BaseImage->GetSizeY();
			int32 Width = BaseImage->GetSizeX();

			for (int32 LOD = 0; LOD < NumLODs; ++LOD)
			{
				const uint8* BaseBuf = BaseImage->GetLODData(LOD);
				uint8* DestBuf = DestImage->GetLODData(LOD);
				const uint8* BlendedBuf = BlendImage->GetLODData(LOD);
				const uint8* MaskBuf = MaskImage->GetLODData(LOD);

				// This could happen in case of missing data files.
				if (!BaseBuf || !BlendedBuf || !MaskBuf || !DestBuf)
				{
					continue;
				}

				// Remove RLE header, mip size and row sizes.
				MaskBuf += sizeof(uint32) + Rows * sizeof(uint32);

				for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
				{
					const uint8* DestRowEnd = DestBuf + Width * BASE_CHANNEL_STRIDE;
					while (DestBuf != DestRowEnd)
					{
						// Decode header
						uint16 Equal;
						FMemory::Memcpy(&Equal, MaskBuf, 2);
						MaskBuf += 2;

						uint8 Different = *MaskBuf;
						++MaskBuf;

						uint8 EqualPixel = *MaskBuf;
						++MaskBuf;

						// Equal pixels
						//check(DestBuf + BASE_CHANNEL_STRIDE * Equal <= BaseImage->GetLODDataSize(0));
						if (EqualPixel == 255)
						{
							for (int32 I = 0; I < Equal; ++I)
							{
								for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
								{
									uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
									uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
									uint32 Result = BLEND_FUNC(Base, Blended);
									if constexpr (CLAMP)
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
									}
									else
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
									}
								}

								// Copy the unblended channels
								// \TODO: unnecessary when doing it in-place?
								if constexpr (UnblendedChannels > 0)
								{
									for (int32 C = 0; C < UnblendedChannels; ++C)
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
									}
								}
							}
						}
						else if (EqualPixel > 0)
						{
							for (int32 I = 0; I < Equal; ++I)
							{
								for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
								{
									uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
									uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
									uint32 Result = BLEND_FUNC_MASKED(Base, Blended, EqualPixel);

									if constexpr (CLAMP)
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
									}
									else
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
									}
								}

								// Copy the unblended channels
								// \TODO: unnecessary when doing it in-place?
								if constexpr (UnblendedChannels > 0)
								{
									for (int32 C = 0; C < UnblendedChannels; ++C)
									{
										DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
									}
								}
							}
						}
						else
						{
							// It could happen if xxxxxOnBase
							if (DestBuf != BaseBuf)
							{
								FMemory::Memmove(DestBuf, BaseBuf, BASE_CHANNEL_STRIDE * Equal);
							}
						}

						DestBuf += BASE_CHANNEL_STRIDE * Equal;
						BaseBuf += BASE_CHANNEL_STRIDE * Equal;
						BlendedBuf += BLENDED_CHANNEL_STRIDE * Equal;

						// Different pixels
						//check(DestBuf + BASE_CHANNEL_STRIDE * Different <= BaseImage->GetDataSize(0));
						for (int32 I = 0; I < Different; ++I)
						{
							for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
							{
								uint32 Mask = MaskBuf[I];
								uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
								uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
								uint32 Result = BLEND_FUNC_MASKED(Base, Blended, Mask);
								if (CLAMP)
								{
									DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
								}
								else
								{
									DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
								}
							}

							// Copy the unblended channels
							// \TODO: unnecessary when doing it in-place?
							if constexpr (UnblendedChannels > 0)
							{
								for (int32 C = 0; C < UnblendedChannels; ++C)
								{
									DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
								}
							}
						}

						DestBuf += BASE_CHANNEL_STRIDE * Different;
						BaseBuf += BASE_CHANNEL_STRIDE * Different;
						BlendedBuf += BLENDED_CHANNEL_STRIDE * Different;
						MaskBuf += Different;
					}
				}

				Rows = FMath::DivideAndRoundUp(Rows, 2);
				Width = FMath::DivideAndRoundUp(Width, 2);
			}
		}
		else
		{
			checkf(false, TEXT("Unsupported mask format."));
		}
	}

	template< uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32),
		bool CLAMP,
		// Number of total channels to actually process
		int32 CHANNELS_TO_BLEND,
		// Number of total channels in the base image
		int32 BASE_CHANNEL_STRIDE,
		// Number of total channels in the blend image
		int32 BLENDED_CHANNEL_STRIDE>
	void BufferLayerFormatEmbeddedMask(
		FImage* DestImage,
		const FImage* BaseImage,
		const FImage* BlendImage,
		uint32 DestOffset,
		uint32 BaseChannelOffset,
		bool bOnlyFirstLOD)
	{
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX() && BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendImage->GetLODCount());

		constexpr int32 UnblendedChannels = BASE_CHANNEL_STRIDE - CHANNELS_TO_BLEND;

		const int32 BytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock; 
	
		const int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		constexpr int32 BatchNumElems = 4096*2;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = DestImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = BytesPerElem;
		BatchArgs.BaseBytesPerElem   = BytesPerElem;
		BatchArgs.BlendBytesPerElem  = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches, 
		[
			&BatchArgs, UnblendedChannels, BlendImageFormat = BlendImage->GetFormat()
		] (int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

			uint8* const DestBuf = BatchViews.Result.GetData();
			uint8 const * const BlendedBuf = BatchViews.Blend.GetData();
			uint8 const * const BaseBuf = BatchViews.Base.GetData(); 

			// This could happen in case of missing data files.
			if (!BaseBuf || !BlendedBuf)
			{
				return;
			}

			if (BlendImageFormat == EImageFormat::RGBA_UByte)
			{
				const uint8* MaskBuf = BlendedBuf + 3;

				for (int32 I = 0; I < BatchViews.NumElems; ++I)
				{
					uint32 Mask = MaskBuf[BLENDED_CHANNEL_STRIDE * I];
					for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
					{
						uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
						uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
						uint32 Result = BLEND_FUNC_MASKED(Base, Blended, Mask);
						if constexpr (CLAMP)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
						}
						else
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
						}
					}
					// Copy the unblended channels
					// \TODO: unnecessary when doing it in-place?
					if constexpr (UnblendedChannels > 0)
					{
						for (int32 C = 0; C < UnblendedChannels; ++C)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
						}
					}
				}
			}
			else
			{
				for (int32 I = 0; I < BatchViews.NumElems; ++I)
				{
					for (int32 C = 0; C < CHANNELS_TO_BLEND; ++C)
					{
						uint32 Base = BaseBuf[BASE_CHANNEL_STRIDE * I + C];
						uint32 Blended = BlendedBuf[BLENDED_CHANNEL_STRIDE * I + C];
						uint32 Result = BLEND_FUNC(Base, Blended);
						if constexpr (CLAMP)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)FMath::Min(255u, Result);
						}
						else
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + C] = (uint8)Result;
						}
					}
					// Copy the unblended channels
					// \TODO: unnecessary when doing it in-place?
					if constexpr (UnblendedChannels > 0)
					{
						for (int32 C = 0; C < UnblendedChannels; ++C)
						{
							DestBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C] = BaseBuf[BASE_CHANNEL_STRIDE * I + CHANNELS_TO_BLEND + C];
						}
					}
				}
			}
		});

	}

	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32),
		bool CLAMP,
		uint32 NC>
    void BufferLayerFormatStrideNoAlpha(
		FImage* DestImage,
		int32 DestOffset,
		int32 Stride,
		const FImage* MaskImage,
		const FImage* BlendImage/*, int32 LODCount*/)
	{
        const uint8* MaskBuf = MaskImage->GetLODData(0);
        const uint8* BlendedBuf = BlendImage->GetLODData(0);
		uint8* DestBuf = DestImage->GetLODData(0) + DestOffset;

		// This could happen in case of missing data files.
		if (!BlendedBuf || !MaskBuf || !DestImage->GetLODData(0))
		{
			return;
		}

		EImageFormat MaskFormat = MaskImage->GetFormat();
        bool bIsUncompressed = (MaskFormat == EImageFormat::L_UByte);

        if (bIsUncompressed)
		{
			int32 RowCount = BlendImage->GetSizeY();
			int32 PixelCount = BlendImage->GetSizeX();
			for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
			{
				for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
				{
					uint32 Mask = *MaskBuf;
					if (Mask)
					{
						for (int32 C = 0; C < NC; ++C)
						{
							uint32 Base = *DestBuf;
							uint32 Blended = *BlendedBuf;
							uint32 Result = BLEND_FUNC(Base, Blended);
							if constexpr (CLAMP)
							{
								*DestBuf = (uint8)FMath::Min(255u, Result);
							}
							else
							{
								*DestBuf = (uint8)Result;
							}
							++DestBuf;
							++BlendedBuf;
						}
					}
					else
					{
						DestBuf += NC;
						BlendedBuf += NC;
					}
					++MaskBuf;
				}

				DestBuf += Stride;
			}
		}
        else if (MaskFormat == EImageFormat::L_UBitRLE)
		{
			int32 Rows = MaskImage->GetSizeY();
			int32 Width = MaskImage->GetSizeX();

            //for (int32 lod = 0; lod < LODCount; ++lod)
            //{
				// Remove RLE header.
                MaskBuf += 4 + Rows*sizeof(uint32);

                for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
                {
                    const uint8* DestRowEnd = DestBuf + Width*NC;
                    while (DestBuf != DestRowEnd)
                    {
                        // Decode header
                        uint16 Zeros = *(const uint16*)MaskBuf;
                        MaskBuf += 2;

                        uint16 Ones = *(const uint16*)MaskBuf;
                        MaskBuf += 2;

                        // Skip
                        DestBuf += Zeros*NC;
                        BlendedBuf += Zeros*NC;

                        // Copy
                        FMemory::Memmove(DestBuf, BlendedBuf, Ones*NC);

                        DestBuf += NC*Ones;
                        BlendedBuf += NC*Ones;
                    }

                    DestBuf += Stride;
                }

                //Rows = FMath::DivideAndRoundUp(Rows, 2);
                //Width = FMath::DivideAndRoundUp(Width, 2);
            //}
		}
		else
		{
			checkf( false, TEXT("Unsupported mask format.") );
		}
	}

	template<uint32(*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
			 uint32(*BLEND_FUNC)(uint32, uint32),
			 bool CLAMP>
    void BufferLayer(FImage* DestImage,
							const FImage* BaseImage,
							const FImage* MaskImage,
                            const FImage* BlendImage,
                            bool bApplyToAlpha,
							bool bOnlyFirstLOD)
	{
		if (BaseImage->GetFormat() == EImageFormat::RGB_UByte)
		{
            BufferLayerFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 3, 3>
                    (DestImage, BaseImage, MaskImage, BlendImage, 0, 0, 0, bOnlyFirstLOD);
		}
        else if (BaseImage->GetFormat() == EImageFormat::RGBA_UByte || 
				 BaseImage->GetFormat() == EImageFormat::BGRA_UByte)
		{
			if (bApplyToAlpha)
			{
				BufferLayerFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 4, 4, 4>
					(DestImage, BaseImage, MaskImage, BlendImage, 0, 0, 0, bOnlyFirstLOD);
			}
			else
			{
				BufferLayerFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 4, 4>
					(DestImage, BaseImage, MaskImage, BlendImage, 0, 0, 0, bOnlyFirstLOD);
			}
		}
		else if (BaseImage->GetFormat() == EImageFormat::L_UByte)
		{
            BufferLayerFormat<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 1, 1, 1>
                    (DestImage, BaseImage, MaskImage, BlendImage, 0, 0, 0, bOnlyFirstLOD);
		}
        else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}


	template< uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*BLEND_FUNC)(uint32, uint32),
		bool CLAMP >
	void BufferLayerEmbeddedMask(
		FImage* DestImage,
		const FImage* BaseImage,
		const FImage* BlendImage,
		bool bApplyToAlpha,
		bool bOnlyFirstLOD)
	{
		if (BaseImage->GetFormat() == EImageFormat::RGB_UByte)
		{
			BufferLayerFormatEmbeddedMask<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 3, 3>
				(DestImage, BaseImage, BlendImage, 0, 0, bOnlyFirstLOD);
		}
		else if (BaseImage->GetFormat() == EImageFormat::RGBA_UByte || 
				 BaseImage->GetFormat() == EImageFormat::BGRA_UByte)
		{
			if (bApplyToAlpha)
			{
				BufferLayerFormatEmbeddedMask<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 4, 4, 4>
					(DestImage, BaseImage, BlendImage, 0, 0, bOnlyFirstLOD);
			}
			else
			{
				BufferLayerFormatEmbeddedMask<BLEND_FUNC_MASKED, BLEND_FUNC, CLAMP, 3, 4, 4>
					(DestImage, BaseImage, BlendImage, 0, 0, bOnlyFirstLOD);
			}
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}


	template< 
		uint32 (*RGB_FUNC_MASKED)(uint32, uint32, uint32),
		uint32 (*A_FUNC)(uint32, uint32),
		bool CLAMP >
	void BufferLayerComposite(
		FImage* BaseImage,
		const FImage* BlendImage,
		bool bOnlyFirstLOD,
		uint8 BlendAlphaSourceChannel)
	{
		check(BaseImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BlendImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX() && BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendImage->GetLODCount());

		bOnlyFirstLOD = bOnlyFirstLOD || BaseImage->GetLODCount() == 1;
		
		int32 FirstLODDataOffset = 0;	
		int32 NumRelevantElems = -1;

		if (BlendImage->Flags & FImage::IF_HAS_RELEVANCY_MAP && bOnlyFirstLOD)
		{
			check(BlendImage->RelevancyMaxY < BaseImage->GetSizeY());
			check(BlendImage->RelevancyMaxY >= BlendImage->RelevancyMinY);

			uint16 SizeX = BaseImage->GetSizeX();
			NumRelevantElems = (BlendImage->RelevancyMaxY - BlendImage->RelevancyMinY + 1) * SizeX * 4;
			
			FirstLODDataOffset = BlendImage->RelevancyMinY * SizeX * 4;
		}

		const int32 BytesPerElem = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock;
		check(BytesPerElem == GetImageFormatData(BlendImage->GetFormat()).BytesPerBlock);

		const int32 LODBegin = 0;
		const int32 LODEnd = BaseImage->GetLODCount();

		constexpr int32 BatchNumElems = 4096*2;

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = LODBegin;
		BatchArgs.LODEnd             = LODEnd;
		BatchArgs.FirstLODOffset     = bOnlyFirstLOD ? FirstLODDataOffset : -1;
		BatchArgs.Result             = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = BytesPerElem;
		BatchArgs.BlendBytesPerElem  = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRangeOffsetViews(BatchArgs);

		// This will always be an upper-bound for bOnlyFirtsLODs, check if it performs as expected or it needs more fine tune.
		const int32 NumRelevantBatches = NumRelevantElems != -1
				? FMath::DivideAndRoundUp(NumRelevantElems, BatchNumElems)
				: NumBatches;

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches, 
		[
			&BatchArgs, BlendAlphaSourceChannel
		] (int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeOffsetViews(BatchId, BatchArgs);

			uint8* BaseBuf = BatchViews.Result.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				// TODO: Optimize this (SIMD?)
				uint32 Mask = BlendBuf[4 * I + 3];

				// RGB
				for (int32 C = 0; C < 3; ++C)
				{
					uint32 Base = BaseBuf[4 * I + C];
					uint32 Blended = BlendBuf[4 * I + C];
					uint32 Result = RGB_FUNC_MASKED(Base, Blended, Mask);
					if constexpr (CLAMP)
					{
						BaseBuf[4 * I + C] = (uint8)FMath::Min(255u, Result);
					}
					else
					{
						BaseBuf[4 * I + C] = (uint8)Result;
					}
				}

				// A
				{
					uint32 Base = BaseBuf[4 * I + 3];
					uint32 Blended = BlendBuf[4 * I + BlendAlphaSourceChannel];
					uint32 Result = A_FUNC(Base, Blended);
					if constexpr (CLAMP)
					{
						BaseBuf[4 * I + 3] = (uint8)FMath::Min(255u, Result);
					}
					else
					{
						BaseBuf[4 * I + 3] = (uint8)Result;
					}
				}
			}
		});
	}

	template<>
	void BufferLayerComposite<BlendChannelMasked, LightenChannel, false>
	(
		FImage* BaseImage,
		const FImage* BlendImage,
		bool bOnlyFirstLOD,
		uint8 BlendAlphaSourceChannel)
	{
		check(BaseImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BlendImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX() && BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendImage->GetLODCount());
		check(BlendAlphaSourceChannel == 3);

		bOnlyFirstLOD = bOnlyFirstLOD || BaseImage->GetLODCount() == 1;

		int32 FirstLODDataOffset = 0;
		int32 NumRelevantElems = -1;
		if (BlendImage->Flags & FImage::IF_HAS_RELEVANCY_MAP && bOnlyFirstLOD)
		{
			check(BlendImage->RelevancyMaxY < BaseImage->GetSizeY());
			check(BlendImage->RelevancyMaxY >= BlendImage->RelevancyMinY);

			uint16 SizeX = BaseImage->GetSizeX();
			NumRelevantElems = (BlendImage->RelevancyMaxY - BlendImage->RelevancyMinY + 1) * SizeX;
			
			FirstLODDataOffset = BlendImage->RelevancyMinY * SizeX * 4;
		}

		const int32 NumLODs = BaseImage->GetLODCount();
	
		constexpr int32 BatchNumElems = 4096 * 2;
		constexpr int32 BytesPerElem = 4;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.FirstLODOffset     = bOnlyFirstLOD ? FirstLODDataOffset : -1;
		BatchArgs.Result             = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = BytesPerElem;
		BatchArgs.BlendBytesPerElem  = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRangeOffsetViews(BatchArgs);

		// This will always be an upper-bound for bOnlyFirtsLODs, check if it performs as expected or it needs more fine tune.
		const int32 NumRelevantBatches = NumRelevantElems != -1
				? FMath::DivideAndRoundUp(NumRelevantElems, BatchNumElems)
				: NumBatches;

		ParallelExecutionUtils::InvokeBatchParallelFor(NumRelevantBatches,
		[
			&BatchArgs
		] (uint32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeOffsetViews(BatchId, BatchArgs);
	
			uint8* BaseBuf = BatchViews.Result.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();
			
			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				// TODO: Optimize this (SIMD?)
				uint32 FullBase;
				FMemory::Memcpy(&FullBase, BaseBuf + I*sizeof(uint32), sizeof(uint32)); 

				uint32 FullBlended;
				FMemory::Memcpy(&FullBlended, BlendBuf + I*sizeof(uint32), sizeof(uint32)); 
				uint32 Mask = (FullBlended & 0xff000000) >> 24;

				uint32 FullResult = 0;
				FullResult |= BlendChannelMasked((FullBase >>  0) & 0xff, (FullBlended >>  0) & 0xff, Mask) << 0;
				FullResult |= BlendChannelMasked((FullBase >>  8) & 0xff, (FullBlended >>  8) & 0xff, Mask) << 8;
				FullResult |= BlendChannelMasked((FullBase >> 16) & 0xff, (FullBlended >> 16) & 0xff, Mask) << 16;
				FullResult |= LightenChannel	((FullBase >> 24) & 0xff, (FullBlended >> 24) & 0xff) << 24;

				FMemory::Memcpy(BaseBuf + I*sizeof(uint32), &FullResult, sizeof(uint32));
			}
		});
	}

	template< 
		VectorRegister4Int (*RGB_FUNC_MASKED)(const VectorRegister4Int&, const VectorRegister4Int&, const VectorRegister4Int&),
		int32 (*A_FUNC)(int32, int32),
		bool CLAMP >
	void BufferLayerCompositeVector(
		FImage* BaseImage,
		const FImage* BlendImage,
		bool bOnlyFirstLOD,
		uint8 BlendAlphaSourceChannel)
	{
		check(BaseImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BlendImage->GetFormat() == EImageFormat::RGBA_UByte);
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX() && BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(bOnlyFirstLOD || BaseImage->GetLODCount() <= BlendImage->GetLODCount());

		const int32 NumLODs = bOnlyFirstLOD ? 1 : BaseImage->GetLODCount();

		constexpr int32 BatchNumElems = 4096*2;
		constexpr int32 BytesPerElem = 4;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = BytesPerElem;
		BatchArgs.BlendBytesPerElem  = BytesPerElem;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches, 
		[
			&BatchArgs, BlendAlphaSourceChannel
		] (uint32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

			uint8* BaseBuf = BatchViews.Result.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				// TODO: Optimize this (SIMD?)
				const int32 BaseAlpha = BaseBuf[4 * I + BlendAlphaSourceChannel];
				const int32 BlendedAlpha = BlendBuf[4 * I + BlendAlphaSourceChannel];

				const VectorRegister4Int Mask = VectorIntSet1(BlendedAlpha);

				uint32 BlendPixel;
				FMemory::Memcpy(&BlendPixel, BlendBuf, sizeof(uint32));

				uint32 BasePixel;
				FMemory::Memcpy(&BasePixel, BaseBuf, sizeof(uint32));

				const VectorRegister4Int Blended = MakeVectorRegisterInt(
						(BlendPixel >> 0) & 0xFF, 
						(BlendPixel >> 8) & 0xFF, 
						(BlendPixel >> 16) & 0xFF, 
						(BlendPixel >> 24) & 0xFF);

				const VectorRegister4Int Base = MakeVectorRegisterInt(
						(BasePixel >> 0) & 0xFF, 
						(BasePixel >> 8) & 0xFF, 
						(BasePixel >> 16) & 0xFF, 
						(BasePixel >> 24) & 0xFF);

				VectorRegister4Int Result = RGB_FUNC_MASKED(Base, Blended, Mask);
				if constexpr (CLAMP)
				{
					Result = VectorIntMin(MakeVectorRegisterIntConstant(255, 255, 255, 255), Result);
				}

				int32 AlphaResult = A_FUNC(BaseAlpha, BlendedAlpha);
				if constexpr (CLAMP)
				{
					AlphaResult = FMath::Min(255, AlphaResult);
				}

				alignas(VectorRegister4Int) int32 IndexableRegister[4];
				VectorIntStoreAligned(Result, &IndexableRegister);

				BaseBuf[4 * I + 0] = static_cast<uint8>(IndexableRegister[0]);
				BaseBuf[4 * I + 1] = static_cast<uint8>(IndexableRegister[1]);
				BaseBuf[4 * I + 2] = static_cast<uint8>(IndexableRegister[2]);
				BaseBuf[4 * I + 3] = static_cast<uint8>(IndexableRegister[3]);

				BaseBuf[4 * I + BlendAlphaSourceChannel] = static_cast<uint8>(AlphaResult);
			}
		});
	}

	template<uint32 (*BLEND_FUNC)(uint32,uint32),
			 bool CLAMP>
    void BufferLayerStrideNoAlpha(FImage* DestImage, int32 DestOffset, int32 Stride, const FImage* MaskImage, const FImage* BlendImage/*, int32 LODCount*/)
	{
		if (BlendImage->GetFormat() == EImageFormat::RGB_UByte)
		{
			BufferLayerFormatStrideNoAlpha<BLEND_FUNC, CLAMP, 3>
					(DestImage, DestOffset, Stride, MaskImage, BlendImage/*, LODCount*/);
		}
        else if (BlendImage->GetFormat() == EImageFormat::RGBA_UByte || 
				 BlendImage->GetFormat() == EImageFormat::BGRA_UByte)
		{
			BufferLayerFormatStrideNoAlpha<BLEND_FUNC, CLAMP, 4>
					(DestImage, DestOffset, Stride, MaskImage, BlendImage/*, LODCount*/);
		}
		else if (BlendImage->GetFormat() == EImageFormat::L_UByte)
		{
			BufferLayerFormatStrideNoAlpha<BLEND_FUNC, CLAMP, 1>
					(DestImage, DestOffset, Stride, MaskImage, BlendImage/*, LODCount*/);
		}
		else
		{
			checkf( false, TEXT("Unsupported format.") );
		}
	}


	/**
	* Apply a blending function to an image with another image as blending layer, on a subrect of
	* the base image.
	* \warning this method applies the blending function to the alpha channel too
	* \warning this method uses the mask as a binary mask (>0)
	*/
	template<uint32 (*BLEND_FUNC)(uint32, uint32), bool CLAMP>
	void ImageLayerOnBaseNoAlpha(
			FImage* BaseImage,
			const FImage* MaskImage,
			const FImage* BlendedImage,
			const box<FIntVector2>& Rect)
	{
		check(BaseImage->GetSizeX() >= Rect.min[0] + Rect.size[0]);
		check(BaseImage->GetSizeY() >= Rect.min[1] + Rect.size[1]);
		check(MaskImage->GetSizeX() == BlendedImage->GetSizeX());
		check(MaskImage->GetSizeY() == BlendedImage->GetSizeY());
		check(BaseImage->GetFormat() == BlendedImage->GetFormat());
		check(MaskImage->GetFormat() == EImageFormat::L_UByte ||
			  //UBYTE_RLE does not look to be supported.
			  //MaskImage->GetFormat() == EImageFormat::L_UByteRLE || 
			  MaskImage->GetFormat() == EImageFormat::L_UBitRLE);
        check(BaseImage->GetLODCount() <= MaskImage->GetLODCount());
        check(BaseImage->GetLODCount() <= BlendedImage->GetLODCount());

		int32 PixelSize = GetImageFormatData(BaseImage->GetFormat()).BytesPerBlock;

		int32 Start = (BaseImage->GetSizeX() * Rect.min[1] + Rect.min[0]) * PixelSize;
		int32 Stride = (BaseImage->GetSizeX() - Rect.size[0]) * PixelSize;

		// Stride is only valid for LOD 0, BufferLayerStride variants cannot operate on multiple lods.
		// TODO: review if this needs to be supported, and implement using a rect lod reducction at this level.
		BufferLayerStrideNoAlpha<BLEND_FUNC, CLAMP>(BaseImage, Start, Stride, MaskImage, BlendedImage/*, BaseImage->GetLODCount()*/);
	}

	template<uint32 NC>
	FORCEINLINE uint32 PackPixel(const uint8* PixelPtr)
	{
		static_assert(NC > 0 && NC <= 4);

		uint32 PixelPack = 0;

		// The compiler should be able to optimize this given that NC is a constant expression.
		FMemory::Memcpy(&PixelPack, PixelPtr, NC);

		return PixelPack;
	}

	template<uint32 NC>
	FORCEINLINE void UnpackPixel(uint8* PixelPtr, uint32 PixelData)
	{
		static_assert(NC > 0 && NC <= 4);

		// The compiler should be able to optimize this given that NC is a constant expression
		FMemory::Memcpy(PixelPtr, &PixelData, NC);
	}

	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		uint32 NC>
	void BufferLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, FVector4f Color, bool bOnlyFirstLOD = false)
	{
		static_assert(NC > 0 && NC <= 4);

		check(BaseImage->GetSizeX() == ResultImage->GetSizeX());
		check(BaseImage->GetSizeY() == ResultImage->GetSizeY());

		const uint32 TopColor = 
			static_cast<uint32>(255.0f * Color[0]) << 0 |  
			static_cast<uint32>(255.0f * Color[1]) << 8 |  
			static_cast<uint32>(255.0f * Color[2]) << 16;  

		const int32 NumLODs = bOnlyFirstLOD ? 1 : ResultImage->GetLODCount();
		
		constexpr int32 BatchNumElems = 4098*2;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.ResultBytesPerElem = NC;
		BatchArgs.BaseBytesPerElem   = NC;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, TopColor
		](int32 BatchId)
		{

			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);
	
			const uint8* BaseBuf = BatchViews.Base.GetData();
			uint8* ResultBuf = BatchViews.Result.GetData();
			
			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				const uint32 Base = PackPixel<NC>(&BaseBuf[NC * I]);

				const uint32 Result = BLEND_FUNC(Base, TopColor);
				UnpackPixel<NC>(&ResultBuf[NC * I], Result);
			}
		});
	}

	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		uint32 NC>
	void BufferLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendImage, bool bOnlyFirstLOD)
	{
		static_assert(NC > 0 && NC <= 4);

		check(BaseImage->GetSizeX() == BlendImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(BaseImage->GetSizeX() == ResultImage->GetSizeX());
		check(BaseImage->GetSizeY() == ResultImage->GetSizeY());
		check(BaseImage->GetFormat() == BlendImage->GetFormat());
		check(BaseImage->GetFormat() == ResultImage->GetFormat());

		const int32 NumLODs = bOnlyFirstLOD ? 1 : ResultImage->GetLODCount();

		constexpr int32 BatchNumElems = 4098*2;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = NC;
		BatchArgs.BaseBytesPerElem   = NC;
		BatchArgs.BlendBytesPerElem  = NC;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);
			
			const uint8* BaseBuf = BatchViews.Base.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();
			uint8* ResultBuf = BatchViews.Result.GetData();
			
			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				const uint32 Base = PackPixel<NC>(&BaseBuf[NC * I]);
				const uint32 Blend = PackPixel<NC>(&BlendBuf[NC * I]);

				const uint32 Result = BLEND_FUNC(Base, Blend);
				UnpackPixel<NC>(&ResultBuf[NC * I], Result);
			}
		});
	}

	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32), 
		uint32 NC>
	void BufferLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, const FImage* BlendImage, bool bOnlyFirstLOD)
	{
		static_assert(NC > 0 && NC <= 4);

		check(BaseImage->GetSizeX() == BlendImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(BaseImage->GetSizeX() == MaskImage->GetSizeX());
		check(BaseImage->GetSizeY() == MaskImage->GetSizeY());
		check(BaseImage->GetFormat() == BlendImage->GetFormat());

		const int32 NumLODs = bOnlyFirstLOD ? 1 : ResultImage->GetLODCount();

		constexpr int32 BatchNumElems = 4098*2;
		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.LODBegin           = 0;
		BatchArgs.LODEnd             = NumLODs;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.Mask               = MaskImage;
		BatchArgs.ResultBytesPerElem = NC;
		BatchArgs.BaseBytesPerElem   = NC;
		BatchArgs.BlendBytesPerElem  = NC;
		BatchArgs.MaskBytesPerElem   = 1;

		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatchesLODRange(BatchArgs);
		
		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchLODRangeViews(BatchId, BatchArgs);

			const uint8* BaseBuf = BatchViews.Base.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();
			const uint8* MaskBuf = BatchViews.Mask.GetData();
			uint8* ResultBuf = BatchViews.Result.GetData();
			
			// This could happen in case of missing data files.
			if (!BaseBuf || !BlendBuf || !MaskBuf)
			{
				return;
			}

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				const uint32 Base = PackPixel<NC>(&BaseBuf[NC * I]);
				const uint32 Blend = PackPixel<NC>(&BlendBuf[NC * I]);
				const uint32 Mask = PackPixel<1>(&MaskBuf[1 * I]);

				const uint32 Result = BLEND_FUNC_MASKED(Base, Blend, Mask);
				UnpackPixel<NC>(&ResultBuf[NC * I], Result);
			}
		});
	}

	template< 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32), 
		uint32 NC>
	void BufferLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Color)
	{
		static_assert(NC > 0 && NC <= 4);

		check(BaseImage->GetSizeX() == MaskImage->GetSizeX());
		check(BaseImage->GetSizeY() == MaskImage->GetSizeY());

		const uint32 TopColor = 
			static_cast<uint32>(255.0f * Color[0]) << 0 |  
			static_cast<uint32>(255.0f * Color[1]) << 8 |  
			static_cast<uint32>(255.0f * Color[2]) << 16;  

		constexpr int32 BatchNumElems = 4098*2;

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.Result             = ResultImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Mask               = MaskImage;
		BatchArgs.ResultBytesPerElem = NC;
		BatchArgs.BaseBytesPerElem   = NC;
		BatchArgs.MaskBytesPerElem   = 1;
		
		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatches(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
		[
			&BatchArgs, TopColor
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchViews(BatchId, BatchArgs);
	
			const uint8* BaseBuf = BatchViews.Base.GetData();
			const uint8* MaskBuf = BatchViews.Mask.GetData();
			uint8* ResultBuf = BatchViews.Result.GetData();

			// This could happen in case of missing data files.
			if (!BaseBuf || !MaskBuf)
			{
				return;
			}

			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				const uint32 Base = PackPixel<NC>(&BaseBuf[NC * I]);
				const uint32 Mask = PackPixel<1>(&MaskBuf[1 * I]);

				const uint32 Result = BLEND_FUNC_MASKED(Base, TopColor, Mask);
				UnpackPixel<NC>(&ResultBuf[NC * I], Result);
			}
		});
	}

	template<uint32 (*BLEND_FUNC)(uint32, uint32)>
	void ImageLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, bool bOnlyFirstLOD)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(bOnlyFirstLOD || ResultImage->GetLODCount() == BaseImage->GetLODCount());
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		check(BaseImage->GetFormat() == BlendedImage->GetFormat());
		check(bOnlyFirstLOD || ResultImage->GetLODCount() <= BlendedImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();

		if (BaseFormat == EImageFormat::L_UByte)
		{
			BufferLayerCombine<BLEND_FUNC, 1>(ResultImage, BaseImage, BlendedImage, bOnlyFirstLOD);
		}
		else if (BaseFormat == EImageFormat::RGB_UByte)
		{
			BufferLayerCombine<BLEND_FUNC, 3>(ResultImage, BaseImage, BlendedImage, bOnlyFirstLOD);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
		{
			// \todo: pass swizzle template argument if BGRA_UBYTE, not yet supported.
			BufferLayerCombine<BLEND_FUNC, 4>(ResultImage, BaseImage, BlendedImage, bOnlyFirstLOD);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}

	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32), 
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32)>
	void ImageLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, const FImage* BlendedImage, bool bOnlyFirstLOD)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(bOnlyFirstLOD || ResultImage->GetLODCount() == BaseImage->GetLODCount());
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		check(BaseImage->GetFormat() == BlendedImage->GetFormat());
		check(bOnlyFirstLOD || ResultImage->GetLODCount() <= BlendedImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();

		TSharedPtr<FImage> TempMaskImage;
		if (MaskImage->GetFormat() != EImageFormat::L_UByte)
		{
			UE_LOG(LogMutableCore, Log, TEXT("Image layer format not supported. A generic one will be used. "));

			FImageOperator ImOp = FImageOperator::GetDefault(nullptr);
			constexpr int32 Quality = 4;
			TempMaskImage = ImOp.ImagePixelFormat( Quality, MaskImage, EImageFormat::L_UByte );
			MaskImage = TempMaskImage.Get();
		}

		if (BaseFormat == EImageFormat::L_UByte)
		{
			BufferLayerCombine<BLEND_FUNC_MASKED, 1>(ResultImage, BaseImage, MaskImage, BlendedImage, bOnlyFirstLOD);
		}
		else if (BaseFormat == EImageFormat::RGB_UByte)
		{
			BufferLayerCombine<BLEND_FUNC_MASKED, 3>(ResultImage, BaseImage, MaskImage, BlendedImage, bOnlyFirstLOD);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
		{
			// \todo: pass swizzle template argument if BGRA_UBYTE, not yet supported.
			BufferLayerCombine<BLEND_FUNC_MASKED, 4>(ResultImage, BaseImage, MaskImage, BlendedImage, bOnlyFirstLOD);
		}
		else
		{
			UE_LOG(LogMutableCore, Log, TEXT("Image layer format not supported. A generic one will be used. "));

			FImageOperator ImOp = FImageOperator::GetDefault(nullptr);
			constexpr int32 Quality = 4;
			TSharedPtr<FImage> TempBaseImage = ImOp.ImagePixelFormat(Quality, BaseImage, EImageFormat::RGBA_UByte);
			TSharedPtr<FImage> TempBlededImage = ImOp.ImagePixelFormat(Quality, BlendedImage, EImageFormat::RGBA_UByte);
			BufferLayerCombine<BLEND_FUNC_MASKED, 4>(ResultImage, TempBaseImage.Get(), MaskImage, TempBlededImage.Get(), bOnlyFirstLOD);
		}
	}

	template<uint32 (*BLEND_FUNC)(uint32, uint32)>
	void ImageLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, FVector4f Color)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(ResultImage->GetLODCount() == BaseImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();

		if (BaseFormat == EImageFormat::L_UByte)
		{
			BufferLayerCombineColour<BLEND_FUNC, 1>(ResultImage, BaseImage, Color);
		}
		else if (BaseFormat == EImageFormat::RGB_UByte)
		{
			BufferLayerCombineColour<BLEND_FUNC, 3>(ResultImage, BaseImage, Color);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
		{
			// \todo: pass swizzle template argument if BGRA_UBYTE, not yet supported.
 			BufferLayerCombineColour<BLEND_FUNC, 4>(ResultImage, BaseImage, Color);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}

	template< 
		uint32 (*BLEND_FUNC)(uint32, uint32),
		uint32 (*BLEND_FUNC_MASKED)(uint32, uint32, uint32)>
	void ImageLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Color)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(ResultImage->GetLODCount() == BaseImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();

		if (MaskImage->GetFormat() != EImageFormat::L_UByte)
		{
			checkf(false, TEXT("Unsupported mask format."));

			BufferLayerCombineColour<BLEND_FUNC, 1>(ResultImage, BaseImage, Color);
		}

		if (BaseFormat == EImageFormat::L_UByte)
		{
			BufferLayerCombineColour<BLEND_FUNC_MASKED, 1>(ResultImage, BaseImage, MaskImage, Color);
		}
		else if (BaseFormat == EImageFormat::RGB_UByte)
		{
			BufferLayerCombineColour<BLEND_FUNC_MASKED, 3>(ResultImage, BaseImage, MaskImage, Color);
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
		{
			// \todo: pass swizzle template argument if BGRA_UBYTE, not yet supported.
 			BufferLayerCombineColour<BLEND_FUNC_MASKED, 4>(ResultImage, BaseImage, MaskImage, Color);
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}

	template<uint32 NCBase, uint32 NCBlend, class ImageCombineFn>
	void BufferLayerCombineFunctor(FImage* DestImage, const FImage* BaseImage, const FImage* BlendImage, ImageCombineFn&& ImageCombine)
	{
		static_assert(NCBase > 0 && NCBase <= 4);
		static_assert(NCBlend > 0 && NCBlend <= 4);

		check(BaseImage->GetFormat() == DestImage->GetFormat());
		check(BaseImage->GetSizeX() == BlendImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendImage->GetSizeY());
		check(BaseImage->GetSizeX() == DestImage->GetSizeX());
		check(BaseImage->GetSizeY() == DestImage->GetSizeY());
		check(BaseImage->GetLODCount() <= BlendImage->GetLODCount());
		check(BaseImage->GetLODCount() <= DestImage->GetLODCount());
	
		constexpr int32 BatchNumElems = 4098*2;

		OpImageLayerInternal::FOpLayerBatchArgs BatchArgs;
		BatchArgs.BatchNumElems      = BatchNumElems;
		BatchArgs.Result             = DestImage;
		BatchArgs.Base               = BaseImage;
		BatchArgs.Blend              = BlendImage;
		BatchArgs.ResultBytesPerElem = NCBase;
		BatchArgs.BaseBytesPerElem   = NCBase;
		BatchArgs.BlendBytesPerElem  = NCBlend;
		
		const int32 NumBatches = OpImageLayerInternal::GetOpLayerNumBatches(BatchArgs);

		ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches, 
		[
			&BatchArgs, ImageCombine
		](int32 BatchId)
		{
			OpImageLayerInternal::FOpLayerBatchViews BatchViews = 
					OpImageLayerInternal::GetOpLayerBatchViews(BatchId, BatchArgs);

			const uint8* BaseBuf = BatchViews.Base.GetData();
			const uint8* BlendBuf = BatchViews.Blend.GetData();
			uint8* DestBuf = BatchViews.Result.GetData();
	
			for (int32 I = 0; I < BatchViews.NumElems; ++I)
			{
				const uint32 Base = PackPixel<NCBase>(&BaseBuf[NCBase * I]);
				const uint32 Blend = PackPixel<NCBlend>(&BlendBuf[NCBlend * I]);

				const uint32 Result = ImageCombine(Base, Blend);
				UnpackPixel<NCBase>(&DestBuf[NCBase* I], Result);
			}
		});
	}

	// Same functionality as above, in this case we use a functor which allows to pass user data. 
	template<class ImageCombineFn>
	void ImageLayerCombineFunctor(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, ImageCombineFn&& ImageCombine)
	{
		check(ResultImage->GetFormat() == BaseImage->GetFormat());
		check(ResultImage->GetSizeX() == BaseImage->GetSizeX());
		check(ResultImage->GetSizeY() == BaseImage->GetSizeY());
		check(ResultImage->GetLODCount() == BaseImage->GetLODCount());
		check(BaseImage->GetSizeX() == BlendedImage->GetSizeX());
		check(BaseImage->GetSizeY() == BlendedImage->GetSizeY());
		check(ResultImage->GetLODCount() <= BlendedImage->GetLODCount());

		const EImageFormat BaseFormat = BaseImage->GetFormat();
		const EImageFormat BlendFormat = BlendedImage->GetFormat();

		if (BaseFormat == EImageFormat::L_UByte )
		{
			if (BlendFormat == EImageFormat::L_UByte )
			{
				BufferLayerCombineFunctor<1, 1>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGB_UByte )
			{
				BufferLayerCombineFunctor<1, 3>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGBA_UByte || BlendFormat == EImageFormat::BGRA_UByte)
			{
				BufferLayerCombineFunctor<1, 4>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else
			{
				checkf(false, TEXT("Unsupported format."));
			}
		}
		else if (BaseFormat == EImageFormat::RGB_UByte)
		{
			if (BlendFormat == EImageFormat::L_UByte )
			{
				BufferLayerCombineFunctor<3, 1>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGB_UByte )
			{
				BufferLayerCombineFunctor<3, 3>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGBA_UByte || BlendFormat == EImageFormat::BGRA_UByte )
			{
				BufferLayerCombineFunctor<3, 4>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else
			{
				checkf(false, TEXT("Unsupported format."));
			}
		}
		else if (BaseFormat == EImageFormat::RGBA_UByte || BaseFormat == EImageFormat::BGRA_UByte)
		{
			if (BlendFormat == EImageFormat::L_UByte )
			{
				BufferLayerCombineFunctor<4, 1>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGB_UByte )
			{
				BufferLayerCombineFunctor<4, 3>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else if (BlendFormat == EImageFormat::RGBA_UByte || BlendFormat == EImageFormat::BGRA_UByte )
			{
				BufferLayerCombineFunctor<4, 4>(ResultImage, BaseImage, BlendedImage, Forward<ImageCombineFn>(ImageCombine));
			}
			else
			{
				checkf(false, TEXT("Unsupported format."));
			}
		}
		else
		{
			checkf(false, TEXT("Unsupported format."));
		}
	}

	//! Blend a subimage on the base using a mask.
	void ImageBlendOnBaseNoAlpha(FImage* BaseImage, const FImage* MaskImage, const FImage* BlendedImage, const box<FIntVector2>& Rect)
	{
		ImageLayerOnBaseNoAlpha<BlendChannel, false>(BaseImage, MaskImage, BlendedImage, Rect);
	}

	template void BufferLayer<SoftLightChannelMasked, SoftLightChannel, false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<HardLightChannelMasked, HardLightChannel, false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<BurnChannelMasked     , BurnChannel     , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<DodgeChannelMasked    , DodgeChannel    , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<ScreenChannelMasked   , ScreenChannel   , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<OverlayChannelMasked  , OverlayChannel  , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<LightenChannelMasked  , LightenChannel  , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<MultiplyChannelMasked , MultiplyChannel , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<BlendChannelMasked    , BlendChannel    , false>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);

	template void BufferLayer<SoftLightChannelMasked, SoftLightChannel, true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<HardLightChannelMasked, HardLightChannel, true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<BurnChannelMasked     , BurnChannel     , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<DodgeChannelMasked    , DodgeChannel    , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<ScreenChannelMasked   , ScreenChannel   , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<OverlayChannelMasked  , OverlayChannel  , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<LightenChannelMasked  , LightenChannel  , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<MultiplyChannelMasked , MultiplyChannel , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);
    template void BufferLayer<BlendChannelMasked    , BlendChannel    , true>(FImage*, const FImage*, const FImage*, const FImage*, bool, bool);

	template void BufferLayerEmbeddedMask<SoftLightChannelMasked, SoftLightChannel, false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<HardLightChannelMasked, HardLightChannel, false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<BurnChannelMasked     , BurnChannel     , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<DodgeChannelMasked    , DodgeChannel    , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<ScreenChannelMasked   , ScreenChannel   , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<OverlayChannelMasked  , OverlayChannel  , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<LightenChannelMasked  , LightenChannel  , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<MultiplyChannelMasked , MultiplyChannel , false>(FImage*, const FImage*, const FImage*, bool, bool); 
	template void BufferLayerEmbeddedMask<BlendChannelMasked    , BlendChannel    , false>(FImage*, const FImage*, const FImage*, bool, bool); 
   
	template void BufferLayer<SoftLightChannel, false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<HardLightChannel, false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<BurnChannel     , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<DodgeChannel    , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<ScreenChannel   , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<OverlayChannel  , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<LightenChannel  , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<MultiplyChannel , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<BlendChannel    , false>(FImage*, const FImage*, const FImage*, bool, bool, bool); 

	template void BufferLayer<SoftLightChannel, true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<HardLightChannel, true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<BurnChannel     , true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<DodgeChannel    , true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<ScreenChannel   , true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<OverlayChannel  , true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<LightenChannel  , true>(FImage*, const FImage*, const FImage*, bool, bool, bool); 
    template void BufferLayer<MultiplyChannel , true>(FImage*, const FImage*, const FImage*, bool, bool, bool);
    template void BufferLayer<BlendChannel    , true>(FImage*, const FImage*, const FImage*, bool, bool, bool);

	template void BufferLayerInPlace<SoftLightChannel, false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<HardLightChannel, false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<BurnChannel     , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<DodgeChannel    , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<ScreenChannel   , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<OverlayChannel  , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<LightenChannel  , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<MultiplyChannel , false, 1>(FImage*, const FImage*, bool, uint32, uint32); 
	template void BufferLayerInPlace<BlendChannel    , false, 1>(FImage*, const FImage*, bool, uint32, uint32);

	template void BufferLayerColour<SoftLightChannelMasked, SoftLightChannel>(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<HardLightChannelMasked, HardLightChannel>(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<BurnChannelMasked     , BurnChannel     >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<DodgeChannelMasked    , DodgeChannel    >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<ScreenChannelMasked   , ScreenChannel   >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<OverlayChannelMasked  , OverlayChannel  >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<LightenChannelMasked  , LightenChannel  >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<MultiplyChannelMasked , MultiplyChannel >(FImage*, const FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<BlendChannelMasked    , BlendChannel    >(FImage*, const FImage*, const FImage*, FVector4f); 

	template void BufferLayerColourFromAlpha<SoftLightChannel>(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<HardLightChannel>(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<BurnChannel     >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<DodgeChannel    >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<ScreenChannel   >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<OverlayChannel  >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<LightenChannel  >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColourFromAlpha<MultiplyChannel >(FImage*, const FImage*, FVector4f); 

	template void BufferLayerColour<SoftLightChannel>(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<HardLightChannel>(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<BurnChannel     >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<DodgeChannel    >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<ScreenChannel   >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<OverlayChannel  >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<LightenChannel  >(FImage*, const FImage*, FVector4f); 
	template void BufferLayerColour<MultiplyChannel >(FImage*, const FImage*, FVector4f); 

	template void BufferLayerColourInPlace<SoftLightChannelMasked, SoftLightChannel, 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<HardLightChannelMasked, HardLightChannel, 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<BurnChannelMasked     , BurnChannel     , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<DodgeChannelMasked    , DodgeChannel    , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<ScreenChannelMasked   , ScreenChannel   , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<OverlayChannelMasked  , OverlayChannel  , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<LightenChannelMasked  , LightenChannel  , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<MultiplyChannelMasked , MultiplyChannel , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<BlendChannelMasked    , BlendChannel    , 1>(FImage*, const FImage*, FVector4f, bool, uint32, uint8); 
	
	template void BufferLayerColourInPlace<SoftLightChannel, 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<HardLightChannel, 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<BurnChannel     , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<DodgeChannel    , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<ScreenChannel   , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<OverlayChannel  , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<LightenChannel  , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<MultiplyChannel , 1>(FImage*, FVector4f, bool, uint32, uint8); 
	template void BufferLayerColourInPlace<BlendChannel    , 1>(FImage*, FVector4f, bool, uint32, uint8); 

	// BufferLayerComposite* uses are specializations, no need to explicitly instanciate.
	//template void BufferLayerComposite<BlendChannelMasked, LightenChannel, false>(FImage*, const FImage*, bool, uint8);

	//template void BufferLayerCompositeVector<VectorBlendChannelMasked, VectorLightenChannel, false>(FImage*, const FImage*, bool, uint8);

	template void ImageLayerCombine<CombineNormal>(FImage*, const FImage*, const FImage*, bool);
	template void ImageLayerCombine<CombineNormal, CombineNormalMasked>(FImage*, const FImage*, const FImage*, const FImage*, bool);

	template void ImageLayerCombineColour<CombineNormal>(FImage*, const FImage*, FVector4f);
	template void ImageLayerCombineColour<CombineNormal, CombineNormalMasked>(FImage*, const FImage*, const FImage*, FVector4f);

	template void ImageLayerCombineFunctor<FNormalCompositeFunctor>(FImage*, const FImage*, const FImage*, FNormalCompositeFunctor&&);
	template void ImageLayerCombineFunctor<FNormalCompositeIdentityFunctor>(FImage*, const FImage*, const FImage*, FNormalCompositeIdentityFunctor&&);

}
