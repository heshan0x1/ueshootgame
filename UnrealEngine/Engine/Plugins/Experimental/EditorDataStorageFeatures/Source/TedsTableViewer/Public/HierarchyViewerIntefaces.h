// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/Handles.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

#define UE_API TEDSTABLEVIEWER_API

namespace UE::Editor::DataStorage
{
	class SHierarchyViewer;
	
	// Data interface for the hierarchy viewer to extract hierarchy information from a row
	class IHierarchyViewerDataInterface
	{
	public:
		UE_API virtual ~IHierarchyViewerDataInterface() = default;
		UE_API virtual RowHandle GetParent(const ICoreProvider& Storage, RowHandle InRow) const = 0;
	};

	// Data interface for the hierarchy viewer that operates on a single FHierarchyHandle 
	class FHierarchyViewerData : public IHierarchyViewerDataInterface
	{
	public:
		UE_API explicit FHierarchyViewerData(FHierarchyHandle InHierarchyHandle);
		UE_API virtual RowHandle GetParent(const ICoreProvider& Storage, RowHandle InRow) const override;
	private:
		FHierarchyHandle HierarchyHandle;
	};

	// Legacy data interface for the hierarchy viewer that supports arbitrary hierarchy column types alongside a function to extract the row handle from the column data
	class FHierarchyViewerLegacyData : public IHierarchyViewerDataInterface
	{
	public:
		UE_API FHierarchyViewerLegacyData(const UScriptStruct* InHierarchyDataColumnType, TFunction<RowHandle(const void*, const UScriptStruct*)> InGetRowHandleFn);
		UE_API virtual RowHandle GetParent(const ICoreProvider& Storage, RowHandle InRow) const override;
		
	protected:
		const UScriptStruct* HierarchyDataColumnType;
		TFunction<RowHandle(const void*, const UScriptStruct*)> GetRowHandleFn;
	};
}

#undef UE_API