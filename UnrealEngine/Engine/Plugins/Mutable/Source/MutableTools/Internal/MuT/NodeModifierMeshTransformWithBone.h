// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/Types.h"
#include "MuR/Skeleton.h"
#include "MuT/Node.h"
#include "MuT/NodeModifier.h"
#include "MuT/NodeMatrix.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

/** */
class NodeModifierMeshTransformWithBone : public NodeModifier
{
public:

	/* */
	FBoneName BoneName;
	float ThresholdFactor;

	/** */
	Ptr<NodeMatrix> MatrixNode;


public:

	// Node interface
	virtual const FNodeType* GetType() const override { return &StaticType; }
	static const FNodeType* GetStaticType() { return &StaticType; }

protected:

	/** Forbidden. Manage with the Ptr<> template. */
	virtual ~NodeModifierMeshTransformWithBone() override {}

private:

	static UE_API FNodeType StaticType;

};

}

#undef UE_API
