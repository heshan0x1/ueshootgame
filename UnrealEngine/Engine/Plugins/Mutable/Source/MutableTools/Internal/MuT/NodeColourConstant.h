// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "Math/Vector4.h"
#include "Math/MathFwd.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** This node outputs a predefined colour value.
	*/
	class NodeColourConstant : public NodeColour
	{
	public:

		FVector4f Value;

	public:

		// Node interface
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeColourConstant() {}

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
