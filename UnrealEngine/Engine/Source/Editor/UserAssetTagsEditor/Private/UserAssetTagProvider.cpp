// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserAssetTagProvider.h"

FText UUserAssetTagProvider::GetDisplayNameText(const UUserAssetTagEditorContext* Context) const
{
	return GetClass()->GetDisplayNameText();
}

FText UUserAssetTagProvider::GetToolTipText(const UUserAssetTagEditorContext* Context) const
{
	return GetClass()->GetToolTipText();
}
