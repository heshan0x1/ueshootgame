// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeImage.h"

#define UE_API MUTABLETOOLS_API


namespace UE::Mutable::Private
{

	/** Obtain a colour by sampling an image at specific homogeneous coordinates.
	*/
	class NodeColourSampleImage : public NodeColour
	{
	public:

		Ptr<NodeImage> Image;
		Ptr<NodeScalar> X;
		Ptr<NodeScalar> Y;

	public:

		// Node interface
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeColourSampleImage() {}

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
