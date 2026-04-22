// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsRowViewNode.h"

namespace UE::Editor::DataStorage::QueryStack
{
	FRowViewNode::FRowViewNode(FRowHandleArray& Rows)
		: Rows(&Rows)
	{}

	void FRowViewNode::MarkDirty()
	{
		Revision++;
	}

	void FRowViewNode::ResetView(FRowHandleArray& InRows)
	{
		Rows = &InRows;
		MarkDirty();
	}

	INode::RevisionId FRowViewNode::GetRevision() const
	{
		return Revision;
	}

	void FRowViewNode::Update() 
	{
	}

	FRowHandleArrayView FRowViewNode::GetRows() const
	{
		return Rows->GetRows();
	}

	FRowHandleArray& FRowViewNode::GetMutableRows()
	{
		// No need to check for validity as there's no function that allows a null pointer. The reason it's a pointer
		// is so it can be reassigned.
		return *Rows;
	}
} // namespace UE::Editor::DataStorage::QueryStack
