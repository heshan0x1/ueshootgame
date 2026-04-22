// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeMaterial.h"
#include "MuT/NodeImage.h"
#include "MuR/Types.h"
#include "Math/Vector4.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{
	/** NodeMaterialConstant class. */
	class NodeMaterialConstant : public NodeMaterial
	{
	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

		int32 MaterialId = INDEX_NONE;

		TMap<FName, FVector4f> ColorValues;
		TMap<FName, float> ScalarValues;
		TMap<FName, Ptr<NodeImage>> ImageValues;

	protected:

		// Forbidden. Manage with the Ptr<> template.
		inline ~NodeMaterialConstant() {}

	private:

		static UE_API FNodeType StaticType;
	};
}

#undef UE_API
