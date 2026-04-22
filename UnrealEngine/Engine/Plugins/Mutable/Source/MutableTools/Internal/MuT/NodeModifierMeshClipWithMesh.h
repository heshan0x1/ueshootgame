// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuT/Node.h"
#include "MuT/NodeModifier.h"
#include "MuT/NodeMesh.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** */
	class NodeModifierMeshClipWithMesh : public NodeModifier
	{
	public:

		/** */
		Ptr<NodeMesh> ClipMesh;

		/** */
		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		inline ~NodeModifierMeshClipWithMesh() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
