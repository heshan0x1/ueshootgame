// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ImagePrivate.h"
#include "MuR/OpImageBlend.h"

namespace UE::Mutable::Private
{	
	/** Used function templates need to be manually instanciated in OpImageLayer.cpp */ 

	template<uint32(*BlendFuncMasked)(uint32, uint32, uint32), uint32(*BlendFunc)(uint32, uint32), bool bClamp>
    void BufferLayer(FImage* DestImage, const FImage* BaseImage, const FImage* MaskImage, const FImage* BlendImage, bool bApplyToAlpha, bool bOnlyFirstLOD);

	template<uint32(*BlendFunc)(uint32, uint32), bool bClamp>
	void BufferLayer(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, bool bApplyToAlpha, bool bOnlyOneMip, bool bUseBlendSourceFromBlendAlpha);

	template<uint32(*BlendFuncMasked)(uint32, uint32, uint32), uint32 (*BlendFunc)(uint32, uint32), bool bClamp>
	void BufferLayerEmbeddedMask(FImage* DestImage, const FImage* BaseImage, const FImage* BlendImage, bool bApplyToAlpha, bool bOnlyFirstLOD);
	
	template<uint32(*BlendFunc)(uint32, uint32), bool bClamp, int32 ChannelCount>
	void BufferLayerInPlace(FImage* BaseImage, const FImage* BlendedImage, bool bOnlyOneMip, uint32 BaseOffset, uint32 BlendedOffset);

	template<uint32(*RGBFuncMasked)(uint32, uint32, uint32), uint32(*AlphaFunc)(uint32, uint32), bool bClamp>
	void BufferLayerComposite(FImage* BaseImage, const FImage* BlendImage, bool bOnlyFirstLOD, uint8 BlendAlphaSourceChannel);

	template<> 
	void BufferLayerComposite<BlendChannelMasked, LightenChannel, false>(FImage*, const FImage*, bool, uint8);

	template<VectorRegister4Int(*RGBFuncMasked)(const VectorRegister4Int&, const VectorRegister4Int&, const VectorRegister4Int&), int32(*AlphaFunc)(int32, int32), bool bClamp>
	void BufferLayerCompositeVector(FImage* BaseImage, const FImage* BlendImage, bool bOnlyFirstLOD, uint8 BlendAlphaSourceChannel);

	template<> 
	void BufferLayerCompositeVector<VectorBlendChannelMasked, VectorLightenChannel, false>(FImage*, const FImage*, bool, uint8);

	/** Blend a subimage on the base using a mask */
	void ImageBlendOnBaseNoAlpha(FImage* BaseImage, const FImage* MaskImage, const FImage* BlendedImage, const box<FIntVector2>& Rect);

	template<uint32 (*BlendFuncMasked)(uint32, uint32, uint32), uint32 (*BlendFunc)(uint32, uint32), uint32 ChannelCount>
	void BufferLayerColourInPlace(FImage* BaseImage, const FImage* MaskImage, FVector4f Color, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColorOffset);

	template<uint32 (*BlendFunc)(uint32, uint32), uint32 ChannelCount>
	void BufferLayerColourInPlace(FImage* BaseImage, FVector4f Color, bool bOnlyOneMip, uint32 BaseOffset, uint8 ColorOffset);

	template<uint32 (*BlendFuncMasked)(uint32, uint32, uint32), uint32 (*BlendFunc)(uint32, uint32)>
	void BufferLayerColour(FImage* DestImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Color);

	template<uint32 (*BlendFunc)(uint32,uint32)>
    void BufferLayerColour(FImage* ResultImage, const FImage* BaseImage, FVector4f Color);

	template<uint32 (*BlendFunc)(uint32, uint32)>
	void BufferLayerColourFromAlpha(FImage* ResultImage, const FImage* BaseImage, FVector4f Color);
	
	template<uint32 (*BlendFunc)(uint32, uint32)>
	void ImageLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, bool bOnlyFirstLOD);

	template<uint32 (*BlendFunc)(uint32, uint32), uint32 (*BlendFuncMasked)(uint32, uint32, uint32)>
	void ImageLayerCombine(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, const FImage* BlendedImage, bool bOnlyFirstLOD);

	template<uint32 (*BlendFunc)(uint32, uint32)>
	void ImageLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, FVector4f Color);

	template<uint32 (*BlendFunc)(uint32, uint32), uint32 (*BlendFuncMasked)(uint32, uint32, uint32)>
	void ImageLayerCombineColour(FImage* ResultImage, const FImage* BaseImage, const FImage* MaskImage, FVector4f Color);

	template<class ImageCombineFn>
	void ImageLayerCombineFunctor(FImage* ResultImage, const FImage* BaseImage, const FImage* BlendedImage, ImageCombineFn&& ImageCombine);

}
