// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorkspaceOutlinerItemDetails.h"

namespace UE::UAF::Editor
{

class FAnimNextModuleItemDetails : public UE::Workspace::IWorkspaceOutlinerItemDetails
{
public:
	FAnimNextModuleItemDetails() = default;
	virtual ~FAnimNextModuleItemDetails() override = default;
	virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const override;
};

}
