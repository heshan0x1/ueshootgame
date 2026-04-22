// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/Types.h"
#include "MuT/Node.h"
#include "MuT/NodeModifier.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeMatrix.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

/** */
class NodeModifierMeshTransformInMesh : public NodeModifier
{
public:

	/** */
	Ptr<NodeMesh> BoundingMesh;

	/** */
	Ptr<NodeMatrix> MatrixNode;
public:

	// Node interface
	virtual const FNodeType* GetType() const override { return &StaticType; }
	static const FNodeType* GetStaticType() { return &StaticType; }

protected:

	/** Forbidden. Manage with the Ptr<> template. */
	virtual ~NodeModifierMeshTransformInMesh() override {}

private:

	static UE_API FNodeType StaticType;

};

}

#undef UE_API
