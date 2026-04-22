// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/Features.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

namespace UE::Editor::DataStorage
{
	class FTedsHierarchyAccessInterface;
	class FTedsHierarchyRegistrar final
	{
	public:
		/**
		 * Registers a hierarchy type with TEDS
		 * This sets up the internal columns and processors to kee the bidirectional relationship in sync
		 */
		FHierarchyHandle RegisterHierarchy(ICoreProvider* InProvider, const FHierarchyRegistrationParams& Params);
		/**
		 * Finds a registered hierarchy by name
		 */
		FHierarchyHandle FindHierarchyByName(const FName& Name) const;
		/**
		 * Gets an interface to add and remove rows from a hierarchy and walk the hierarchy
		 */
		const FTedsHierarchyAccessInterface* GetAccessInterface(FHierarchyHandle) const;

		/**
		 * Iterate over all registered hierarchyes
		 */
		void ListHierarchyNames(TFunctionRef<void(const FName& HierarchyName)> Callback) const;
		
	private:
		void RegisterObservers(ICoreProvider& CoreProvider, const FHierarchyHandle& Handle);
		void RegisterProcessors(ICoreProvider& CoreProvider, const FHierarchyHandle& Handle);
		
		struct FRegisteredHierarchy
		{
			FName Name;
			const UScriptStruct* ChildTag;
			const UScriptStruct* ParentTag;
			const UScriptStruct* HierarchyData;
			const UScriptStruct* UnresolvedParentColumn;
			const UScriptStruct* ParentChangedColumn;
			TUniquePtr<FTedsHierarchyAccessInterface> AccessInterface;
		};

		const FRegisteredHierarchy* FindRegisteredHierarchy(const FHierarchyHandle& Handle);
	
	private:
		TArray<FRegisteredHierarchy> RegisteredHierarchies;
	};

	class FTedsHierarchyAccessInterface final
	{
	public:
		struct FConstructParams
		{
			const UScriptStruct* ChildTag;
			const UScriptStruct* ParentTag;
			const UScriptStruct* HierarchyData;
			const UScriptStruct* UnresolvedParentColumn;
			const UScriptStruct* ParentChangedColumn;
		};
		
		explicit FTedsHierarchyAccessInterface(const FConstructParams& Params);
		
		const UScriptStruct* GetChildTagType() const;
		const UScriptStruct* GetParentTagType() const;
		const UScriptStruct* GetHierarchyDataColumnType() const;
		const UScriptStruct* GetUnresolvedParentColumnType() const;

		void SetParentRow(ICoreProvider* CoreProvider, RowHandle Target, RowHandle Parent) const;
		void SetUnresolvedParent(ICoreProvider* CoreProvider, RowHandle Target, FMapKey ParentId, FName MappingDomain) const;
		RowHandle GetParentRow(const ICoreProvider* CoreProvider, RowHandle Target) const;
		
		bool HasChildren(const ICoreProvider& Context, RowHandle Row) const;
		void WalkDepthFirst(
			const ICoreProvider& Context,
			RowHandle Row,
			TFunction<void(const ICoreProvider& Context, RowHandle Owner, RowHandle Target)> OnVisitedFn) const;
		
		TFunction<RowHandle(const void*, const UScriptStruct*)> CreateParentExtractionFunction() const;
		
		void SetParentRow(IQueryContext& Context, RowHandle Target, RowHandle Parent) const;
		void SetUnresolvedParent(IQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const;
		RowHandle GetParentRow(const IQueryContext& Context, RowHandle Target) const;
		void SetParentRow(ISubqueryContext& Context, RowHandle Target, RowHandle Parent) const;
		void SetUnresolvedParent(ISubqueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const;
		RowHandle GetParentRow(const ISubqueryContext& Context, RowHandle Target) const;
		void SetParentRow(IDirectQueryContext& Context, RowHandle Target, RowHandle Parent) const;
		void SetUnresolvedParent(IDirectQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const;
		RowHandle GetParentRow(const IDirectQueryContext& Context, RowHandle Target) const;

	private:

		void SetParent(ICommonQueryContext& Context, RowHandle Target, RowHandle Parent) const;
		void SetUnresolvedParent(ICommonQueryContext& Context, RowHandle Target, FMapKey ParentId, FName MappingDomain) const;
		RowHandle GetParent(const ICommonQueryContext& Context, RowHandle Target) const;
		
		const UScriptStruct* ChildTag;
		const UScriptStruct* ParentTag;
		const UScriptStruct* HierarchyDataColumnType;
		const UScriptStruct* UnresolvedParentColumnType;
		const UScriptStruct* ParentChangedColumn;
	};
}
