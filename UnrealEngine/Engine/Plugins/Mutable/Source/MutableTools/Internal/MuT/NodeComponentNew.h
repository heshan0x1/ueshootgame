// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeComponent.h"
#include "MuT/NodeMaterial.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{

	class NodeComponentNew : public NodeComponent
	{
	public:

		/** Externally managed id assigned to this component. */
		uint16 Id = 0;

		Ptr<NodeMaterial> OverlayMaterial;

	public:

		//-----------------------------------------------------------------------------------------
        // Node interface
		//-----------------------------------------------------------------------------------------		

		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// NodeComponent interface
		//-----------------------------------------------------------------------------------------		
		virtual const class NodeComponentNew* GetParentComponentNew() const override { return this;  }

		//-----------------------------------------------------------------------------------------
        // Own interface
		//-----------------------------------------------------------------------------------------

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeComponentNew() {}

	private:

		static UE_API FNodeType StaticType;
		
	};

}

#undef UE_API
