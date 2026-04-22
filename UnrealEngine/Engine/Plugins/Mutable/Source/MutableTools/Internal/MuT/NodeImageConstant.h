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

	/** Node that outputs a constant image.
	* This node also supports "image references".
	*/
	class NodeImageConstant : public NodeImage
	{
	public:

		/** */
		Ptr<TResourceProxy<FImage>> Proxy;
		
		/** */
		FSourceDataDescriptor SourceDataDescriptor;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		// Own Interface

        //! Set the image to be output by this node
        UE_API void SetValue(TSharedPtr<const FImage>);

        //! Set the image proxy that will provide the image for this node when necessary
        UE_API void SetValue( Ptr<TResourceProxy<FImage>> );

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeImageConstant() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
