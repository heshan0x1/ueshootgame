// Copyright Epic Games, Inc. All Rights Reserved

#include "TedsRowFilterNode.h"

namespace UE::Editor::DataStorage::QueryStack
{
	FRowHandleArrayView FRowFilterNode::GetRows() const
	{
		return Rows.GetRows();
	}

	FRowHandleArray& FRowFilterNode::GetMutableRows()
	{
		return Rows;
	}

	IRowNode::RevisionId FRowFilterNode::GetRevision() const
	{
		return CachedParentRevisionID;
	}

	void FRowFilterNode::Update()
	{
		ParentRowNode->Update();
		if (CachedParentRevisionID != ParentRowNode->GetRevision())
		{
			Rows.Reset();
			Storage->FilterRowsBy(Rows, ParentRowNode->GetRows(), Options, Filter);

			CachedParentRevisionID = ParentRowNode->GetRevision();
		}
	}

	void FRowFilterNode::ForceRefresh()
	{
		CachedParentRevisionID = 0;
	}
} // namespace UE::Editor::DataStorage::QueryStack
