// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeScalar.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** Generate mimaps for an image. */
    class NodeImageMipmap : public NodeImage
	{
	public:

		Ptr<NodeImage> Source;
		Ptr<NodeScalar> Factor;

		FMipmapGenerationSettings Settings;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeImageMipmap() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
