// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/NameTypes.h"

#define UE_API METASOUNDFRONTEND_API

struct FMetaSoundFrontendDocumentBuilder;

namespace Metasound::Frontend
{
	class UE_EXPERIMENTAL(5.7, "Node update transforms are experimental") INodeUpdateTransform
	{
#if WITH_EDITORONLY_DATA
	public:
		virtual ~INodeUpdateTransform() = default;

		// Update document state for a given node
		// This API is in progress and will likely change in the future
		UE_INTERNAL virtual void Update(FMetaSoundFrontendDocumentBuilder& InOutBuilder, const FGuid& InNodeID, const FGuid* InPageID) const = 0;

		// Whether the transform should be automatically applied to node classes registered with the transform
		virtual bool ShouldAutoApply() const = 0;
#endif // if WITH_EDITORONLY_DATA
	};
} // namespace Metasound::Frontend
#undef UE_API
