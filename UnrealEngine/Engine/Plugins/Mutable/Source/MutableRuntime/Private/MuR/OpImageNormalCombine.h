// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ImagePrivate.h"
#include "MuR/OpImageLayer.h"
#include "MuR/OpImageBlend.h"

namespace UE::Mutable::Private
{

	inline void ImageNormalCombine(FImage* pResult, const FImage* pBase, FVector4f col)
	{
		ImageLayerCombineColour<CombineNormal>(pResult, pBase, col);
	}

	inline void ImageNormalCombine(FImage* pResult, const FImage* pBase, const FImage* pBlended, bool bOnlyFirstLOD)
	{
		ImageLayerCombine<CombineNormal>(pResult, pBase, pBlended, bOnlyFirstLOD);
	}

	inline void ImageNormalCombine(FImage* pResult, const FImage* pBase, const FImage* pMask, FVector4f col)
	{
		ImageLayerCombineColour<CombineNormal, CombineNormalMasked>(pResult, pBase, pMask, col);
	}

	inline void ImageNormalCombine(FImage* pResult, const FImage* pBase, const FImage* pMask, const FImage* pBlended, bool bOnlyFirstLOD)
	{
		ImageLayerCombine<CombineNormal, CombineNormalMasked>(pResult, pBase, pMask, pBlended, bOnlyFirstLOD);
	}
}
