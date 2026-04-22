// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/Node.h"
#include "MuT/NodeImage.h"
#include "MuR/Ptr.h"
#include "MuT/NodeMaterial.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{
	/** NodeImageMaterialBreak class. */
	class NodeImageMaterialBreak : public NodeImage
	{
	public:

		Ptr<NodeMaterial> MaterialSource;
		FName ParameterName;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeImageMaterialBreak() {}

	private:

		static UE_API FNodeType StaticType;
	};
}

#undef UE_API
