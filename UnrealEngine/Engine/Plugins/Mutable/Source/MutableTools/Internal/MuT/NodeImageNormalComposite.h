// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** Node that inverts the colors of an image, channel by channel. */
	class NodeImageNormalComposite : public NodeImage
	{
	public:

		Ptr<NodeImage> Base;
		Ptr<NodeImage> Normal;
		float Power = 1.0f;
		ECompositeImageMode Mode;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeImageNormalComposite() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
