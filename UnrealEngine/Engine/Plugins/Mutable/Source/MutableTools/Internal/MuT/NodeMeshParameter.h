// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeRange.h"
#include "MuT/NodeLayout.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	/** */
	class NodeMeshParameter : public NodeMesh
	{
	public:
		
		TSharedPtr<FMesh> ReferenceMesh;

		TArray<Ptr<NodeLayout>> Layouts;

		uint8 LODIndex = 0;

		uint8 SectionIndex = 0;

		bool bAutomaticLODs = false;
		
		/** Name of the parameter */
		FString Name;

		/** User provided ID to identify the parameter. */
		FString UID;

		/** Ranges for the parameter in case it is multidimensional. */
		TArray<Ptr<NodeRange>> Ranges;

	public:

		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeMeshParameter() {}

	private:

		static UE_API FNodeType StaticType;

	};

}

#undef UE_API
