// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "TedsQueryStackInterfaces.h"
#include "TedsRowFilterNode.h"
#include "TedsRowQueryResultsNode.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::Editor::DataStorage
{
	namespace QueryStack
	{
		class FRowQueryResultsNode;
	}

	/*
	 * A search bar widget that can be used to search across a TEDS table viewer widget using a QueryStack Search node.
	 * Example future usage:
	 * 
	 *	SNew(STedsSearchBox)
	 *	.OutSearchNode(&OutSearchNode) // TSharedPtr<QueryStack::IRowNode> OutSearchNode
	 *	.InSearchableRowNode(ReferenceQueryResultsNode)
	 */
	class STedsSearchBox : public SCompoundWidget
	{
	public:
		
		SLATE_BEGIN_ARGS(STedsSearchBox) {}

		// The node whose rows we'll perform a search on
		SLATE_ARGUMENT(TSharedPtr<QueryStack::IRowNode>, InSearchableRowNode)

		// The search results node we create and receive back
		SLATE_ARGUMENT(TSharedPtr<QueryStack::IRowNode>*, OutSearchNode)
			
		SLATE_END_ARGS()

	public:
		
		TEDSTABLEVIEWER_API void Construct(const FArguments& InArgs);

	private:
		void OnTextChanged(const FText& InSearchText);

		/** Internal SharedPtr of the OutSearchNode so that when text is updated, it can ForceRefresh the node */
		TSharedPtr<QueryStack::FRowFilterNode> OutSearchNode;
		FText SearchText;
	};
}