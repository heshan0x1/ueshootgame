// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TedsQueryStackInterfaces.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

#define UE_API TEDSTABLEVIEWER_API

namespace UE::Editor::DataStorage
{
	class IHierarchyViewerDataInterface;
}

namespace UE::Editor::DataStorage::QueryStack
{
	// A custom query stack node to get top level rows of the given rows to display in the hierarchy viewer
	class FTopLevelRowsNode : public IRowNode
	{
	public:
		
		UE_API FTopLevelRowsNode(const ICoreProvider* InStorage, TSharedPtr<IHierarchyViewerDataInterface> InHierarchyData, TSharedPtr<IRowNode> InParent);
		UE_API virtual ~FTopLevelRowsNode() override = default;
		
		/** Retrieve access to the rows used by this node. */
		virtual FRowHandleArrayView GetRows() const override;
		virtual FRowHandleArray& GetMutableRows() override;
		virtual RevisionId GetRevision() const override;
		virtual void Update() override;

	protected:
		void UpdateRows();
	protected:
		RevisionId Revision = 0;
		const ICoreProvider* Storage = nullptr;
		TSharedPtr<IHierarchyViewerDataInterface> HierarchyData;
		TSharedPtr<IRowNode> Parent;
		FRowHandleArray Rows;
		RevisionId CachedParentRevision = TNumericLimits<RevisionId>::Max();
	};
}

#undef UE_API