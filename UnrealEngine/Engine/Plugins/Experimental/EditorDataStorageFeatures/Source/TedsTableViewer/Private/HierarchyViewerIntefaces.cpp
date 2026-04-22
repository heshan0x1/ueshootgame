// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyViewerIntefaces.h"

namespace UE::Editor::DataStorage
{
	FHierarchyViewerData::FHierarchyViewerData(FHierarchyHandle InHierarchyHandle)
		: HierarchyHandle(InHierarchyHandle)
	{
	}

	RowHandle FHierarchyViewerData::GetParent(const ICoreProvider& Storage, RowHandle InRow) const
	{
		return Storage.GetParentRow(HierarchyHandle, InRow);
	}

	FHierarchyViewerLegacyData::FHierarchyViewerLegacyData(const UScriptStruct* InHierarchyDataColumnType,
	                                                       TFunction<RowHandle(const void*, const UScriptStruct*)> InGetRowHandleFn)
		: HierarchyDataColumnType(InHierarchyDataColumnType)
		, GetRowHandleFn(InGetRowHandleFn)
	{
		checkf(HierarchyDataColumnType != nullptr, TEXT("Nullptr ParentColumnType"));
	}

	RowHandle FHierarchyViewerLegacyData::GetParent(const ICoreProvider& Storage, RowHandle InRow) const
	{
		if (const void* ParentColumn = Storage.GetColumnData(InRow, HierarchyDataColumnType))
		{
			RowHandle ParentRow = GetRowHandleFn(ParentColumn, HierarchyDataColumnType);
			return ParentRow;
		}

		return InvalidRowHandle;
	}
}