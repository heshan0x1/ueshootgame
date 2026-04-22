// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorkspaceAssetRegistryInfo.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorkspaceAssetRegistryInfo)

FWorkspaceOutlinerItemExport& FWorkspaceOutlinerItemExport::GetResolvedExport()
{
	if (HasData() && Data.GetScriptStruct() == FWorkspaceOutlinerAssetReferenceItemData::StaticStruct())
	{
		return Data.GetMutable<FWorkspaceOutlinerAssetReferenceItemData>().ReferredExport;
	}

	return *this;
}

const FWorkspaceOutlinerItemExport& FWorkspaceOutlinerItemExport::GetResolvedExport() const
{
	if (HasData() && Data.GetScriptStruct() == FWorkspaceOutlinerAssetReferenceItemData::StaticStruct())
	{
		return Data.Get<FWorkspaceOutlinerAssetReferenceItemData>().ReferredExport;
	}

	return *this;
}
