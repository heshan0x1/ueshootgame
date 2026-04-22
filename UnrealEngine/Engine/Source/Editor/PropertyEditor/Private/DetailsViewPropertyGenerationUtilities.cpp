// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsViewPropertyGenerationUtilities.h"

FDetailsViewPropertyGenerationUtilities::FDetailsViewPropertyGenerationUtilities(SDetailsViewBase& InDetailsView)
	: DetailsViewPtr(StaticCastWeakPtr<SDetailsViewBase>(InDetailsView.AsWeak()))
{
}

const FCustomPropertyTypeLayoutMap& FDetailsViewPropertyGenerationUtilities::GetInstancedPropertyTypeLayoutMap() const
{
	if (TSharedPtr<SDetailsViewBase> DetailsView = DetailsViewPtr.Pin())
	{
		return DetailsView->GetCustomPropertyTypeLayoutMap();
	}

	static FCustomPropertyTypeLayoutMap Empty;
	return Empty;
}

void FDetailsViewPropertyGenerationUtilities::RebuildTreeNodes()
{
	if (TSharedPtr<SDetailsViewBase> DetailsView = DetailsViewPtr.Pin())
	{
		DetailsView->RerunCurrentFilter();
	}
}