// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"

#include "MuR/ImagePrivate.h"
#include "MuR/OpImageLayer.h"
#include "MuR/OpImageBlend.h"

namespace UE::Mutable::Private
{

	inline void ImageNormalComposite(FImage* pResult, const FImage* pBase, const FImage* pNormal, ECompositeImageMode mode, float power)
	{
		if (mode == ECompositeImageMode::CIM_Disabled)
		{
			// Copy base to result. Using functor layer combine with identity to do the copy for simplicity.
			// This could be optimized but will never happen since it is checked at compile time. 	
			ImageLayerCombineFunctor( pResult, pBase, pNormal, FNormalCompositeIdentityFunctor{}); 

			return;
		}	

		const uint8 channel = [mode]() 
			{
				switch (mode)
				{
					case ECompositeImageMode::CIM_NormalRoughnessToRed   : return 0u;
					case ECompositeImageMode::CIM_NormalRoughnessToGreen : return 1u;
					case ECompositeImageMode::CIM_NormalRoughnessToBlue  : return 2u;
					case ECompositeImageMode::CIM_NormalRoughnessToAlpha : return 3u;
				}
			
				check(false);
				return 0u;		
			}();

		ImageLayerCombineFunctor( pResult, pBase, pNormal, FNormalCompositeFunctor{channel, power});
	}
}
