// Copyright Epic Games, Inc. All Rights Reserved.

#include "RowQueryCallbackResultsNode.h"

#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementQueryStorageInterfaces.h"

namespace UE::Editor::DataStorage::QueryStack
{
	FRowQueryCallbackResultsNode::FRowQueryCallbackResultsNode(
		ICoreProvider& Storage,
		TSharedPtr<IQueryNode> InQueryNode,
		ESyncFlags SyncFlags,
		CallbackFn InCallbackFn)
		: FRowQueryResultsNode(Storage, InQueryNode, SyncFlags)
		, Callback(InCallbackFn)
	{
	}

	void FRowQueryCallbackResultsNode::RefreshInternal(FRowHandleArray& TargetRows)
	{
		using namespace UE::Editor::DataStorage::Queries;
		Storage.RunQuery(QueryNode->GetQuery(), 
			EDirectQueryExecutionFlags::AllowBoundQueries,
			CreateDirectQueryCallbackBinding(
		[&TargetRows, this](IDirectQueryContext& Context, const RowHandle* QueryRowResults)
		{
			auto EmitRows = [&TargetRows](TArrayView<const RowHandle> InRows)
			{
				TargetRows.Append(InRows);
			};

			TArrayView<const RowHandle> QueryRowResultsView = MakeArrayView(QueryRowResults, Context.GetRowCount());

			Callback(Context, QueryRowResultsView, EmitRows);
		}));
	}
}
