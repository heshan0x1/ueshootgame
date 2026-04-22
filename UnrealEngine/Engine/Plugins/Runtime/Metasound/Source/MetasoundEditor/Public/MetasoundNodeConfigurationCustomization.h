// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "InstancedStructDetails.h"

#define UE_API METASOUNDEDITOR_API

// Forward Declarations
struct FPropertyChangedEvent;
class FInstancedStructDataDetails;
class IDetailPropertyRow;
class IPropertyHandle;
class UMetasoundEditorGraphNode;

namespace Metasound::Editor
{
	class FMetaSoundNodeConfigurationDataDetails : public FInstancedStructDataDetails
	{
	public:
		FMetaSoundNodeConfigurationDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, TWeakObjectPtr<UMetasoundEditorGraphNode> InNode)
			: FInstancedStructDataDetails(InStructProperty)
			, GraphNode(InNode)
		{
		}

		UE_API virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override;

	protected:
		UE_API void OnChildPropertyChanged(const FPropertyChangedEvent& InPropertyChangedEvent);

		TWeakObjectPtr<UMetasoundEditorGraphNode> GraphNode;
	};
} // namespace Metasound::Editor

#undef UE_API
