// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMaterial.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

    /** NodeMaterialSwitch class. */
	class NodeMaterialSwitch : public NodeMaterial
	{
	public:

		Ptr<NodeScalar> Parameter;

		TArray<Ptr<NodeMaterial>> Options;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		// Forbidden. Manage with the Ptr<> template.
		inline ~NodeMaterialSwitch() {}

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
