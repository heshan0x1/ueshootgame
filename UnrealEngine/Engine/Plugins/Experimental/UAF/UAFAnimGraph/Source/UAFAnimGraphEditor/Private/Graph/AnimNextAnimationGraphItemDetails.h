// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/AnimNextAssetItemDetails.h"

namespace UE::UAF::Editor
{

class FAnimNextAnimationGraphItemDetails : public FAnimNextAssetItemDetails
{
public:
	FAnimNextAnimationGraphItemDetails() = default;
	
	UAFANIMGRAPHEDITOR_API virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const override;

};

}
