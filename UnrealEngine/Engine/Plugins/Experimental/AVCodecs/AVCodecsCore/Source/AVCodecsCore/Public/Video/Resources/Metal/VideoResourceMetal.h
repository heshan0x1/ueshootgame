// Copyright Epic Games, Inc. All Rights Reserved.

#if AVCODECS_USE_METAL

#pragma once

#include "Templates/RefCounting.h"

#include "AVContext.h"
#include "Video/VideoResource.h"
#include "Containers/ResourceArray.h"

THIRD_PARTY_INCLUDES_START
#include "MetalInclude.h"
THIRD_PARTY_INCLUDES_END


/**
 * Metal platform video context and resource.
 */

class FVideoContextMetal : public FAVContext
{
public:
	MTL::Device* Device;

	AVCODECSCORE_API FVideoContextMetal(MTL::Device* Device);
};


class FVideoResourceMetal : public TVideoResource<FVideoContextMetal>
{
private:
    MTL::Texture* Raw;

public:
	static AVCODECSCORE_API FVideoDescriptor GetDescriptorFrom(TSharedRef<FAVDevice> const& Device, MTL::Texture* Raw);

	inline MTL::Texture* GetRaw() const { return Raw; }

	AVCODECSCORE_API FVideoResourceMetal(TSharedRef<FAVDevice> const& Device, MTL::Texture* Raw, FAVLayout const& Layout);
    AVCODECSCORE_API virtual ~FVideoResourceMetal() override;
	
	AVCODECSCORE_API FAVResult CopyFrom(CVPixelBufferRef Other);
    
	AVCODECSCORE_API virtual FAVResult Validate() const override;
};

DECLARE_TYPEID(FVideoContextMetal, AVCODECSCORE_API);
DECLARE_TYPEID(FVideoResourceMetal, AVCODECSCORE_API);

#endif
