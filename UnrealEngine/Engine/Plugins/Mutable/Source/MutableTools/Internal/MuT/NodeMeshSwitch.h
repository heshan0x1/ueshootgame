// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeScalar.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** This node selects an output Mesh from a set of input Meshs based on a parameter. */
	class NodeMeshSwitch : public NodeMesh
	{
	public:

		Ptr<NodeScalar> Parameter;

		TArray<Ptr<NodeMesh>> Options;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeMeshSwitch() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
