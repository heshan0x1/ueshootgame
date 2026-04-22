// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ExtensionData.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/NodeExtensionData.h"

#define UE_API MUTABLETOOLS_API


namespace UE::Mutable::Private
{

	//! Node that outputs a constant ExtensionData
	//! \ingroup model
	class NodeExtensionDataConstant : public NodeExtensionData
	{
	public:

		TSharedPtr<const FExtensionData> Value;

	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		/** Deprecated. Access Value attribute directly. */
		void SetValue(const TSharedPtr<const FExtensionData>& In) { Value = In; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeExtensionDataConstant() {}

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
