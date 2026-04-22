// Copyright Epic Games, Inc. All Rights Reserved
 
#pragma once

#include "DataStorage/CommonTypes.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"
#include "TedsQueryStackInterfaces.h"

namespace UE::Editor::DataStorage
{
	class ICoreProvider;
}

namespace UE::Editor::DataStorage::QueryStack
{
	/**
	 * Query stack node that filters out rows based on the provided filter.
	 * Performance-wise this will be slower than using filters on queries, but this node can be used when using
	 * query level filters is not possible.
	 */
	class FRowFilterNode final : public IRowNode
	{
	public:
		FRowFilterNode(ICoreProvider* Storage, const TSharedPtr<IRowNode>& ParentRowNode, ICoreProvider::EFilterOptions Options, 
			const Queries::TQueryFunction<bool>& Filter)
			: Storage(Storage)
			, Filter(Filter)
			, ParentRowNode(ParentRowNode)
			, CachedParentRevisionID(ParentRowNode->GetRevision())
			, Options(Options)
		{
		}
    
		FRowFilterNode(ICoreProvider* Storage, const TSharedPtr<IRowNode>& ParentRowNode, const Queries::TQueryFunction<bool>& Filter)
			: FRowFilterNode(
				Storage, 
				ParentRowNode, 
				ICoreProvider::EFilterOptions::Inclusive, 
				Filter)
		{}
            
		/*
		 * Require that the template type cannot already be a TQueryFunction so it will pass to the defined constructor instead of this one.
		 */
		template< typename FilterFunction UE_REQUIRES(
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, Queries::TQueryFunction<bool>>) >
		FRowFilterNode(ICoreProvider* Storage, const TSharedPtr<IRowNode>& ParentRowNode, ICoreProvider::EFilterOptions Options, 
			FilterFunction&& Filter)
			: FRowFilterNode(
				Storage, 
				ParentRowNode, 
				Options, 
				Queries::BuildQueryFunction<bool>(Forward<FilterFunction>(Filter)))
		{}

		/*
		 * Require that the template type cannot already be a TQueryFunction so it will pass to the defined constructor instead of this one.
		 */
		template< typename FilterFunction UE_REQUIRES(
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, Queries::TQueryFunction<bool>>) >
		FRowFilterNode(ICoreProvider* Storage, const TSharedPtr<IRowNode>& ParentRowNode, FilterFunction&& Filter)
			: FRowFilterNode(
				Storage, 
				ParentRowNode, 
				ICoreProvider::EFilterOptions::Inclusive, 
				Queries::BuildQueryFunction<bool>(Forward<FilterFunction>(Filter)))
		{}

		virtual ~FRowFilterNode() override = default;

		TEDSQUERYSTACK_API virtual FRowHandleArrayView GetRows() const override;
		TEDSQUERYSTACK_API virtual FRowHandleArray& GetMutableRows() override;
		TEDSQUERYSTACK_API virtual RevisionId GetRevision() const override;
		TEDSQUERYSTACK_API virtual void Update() override;
		
		/** Call this when external inputs into the filter, such as search strings or filtering groups, have been updated. */
		TEDSQUERYSTACK_API void ForceRefresh();

	private:
		ICoreProvider* Storage;
		Queries::TQueryFunction<bool> Filter;
		FRowHandleArray Rows;
		TSharedPtr<IRowNode> ParentRowNode;
		RevisionId CachedParentRevisionID;
		ICoreProvider::EFilterOptions Options;
	};
}
