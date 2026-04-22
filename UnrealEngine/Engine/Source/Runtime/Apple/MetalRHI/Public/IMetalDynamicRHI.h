// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "RHI.h"
#include "MetalThirdParty.h"

struct IMetalDynamicRHI : public FDynamicRHI
{
	virtual ERHIInterfaceType GetInterfaceType() const override final
	{
		return ERHIInterfaceType::Metal;
	}
	
	virtual void 			  RHIRunOnQueue(TFunction<void(MTL::CommandQueue*)>&& CodeToRun, bool bWaitForSubmission) = 0;
	virtual FTextureRHIRef	  RHICreateTexture2DFromCVMetalTexture(EPixelFormat Format, ETextureCreateFlags TexCreateFlags, const FClearValueBinding& ClearValueBinding, CVMetalTextureRef Resource) = 0;
};

inline bool IsRHIMetal()
{
	return GDynamicRHI != nullptr && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Metal;
}

inline IMetalDynamicRHI* GetIMetalDynamicRHI()
{
	check(GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::Metal);
	return GetDynamicRHI<IMetalDynamicRHI>();
}

