// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowAttachment.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsAssetDataflowContent.h"

void UDataflowAttachment::WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const
{
	if(const TObjectPtr<UPhysicsAssetDataflowContent> Content = Cast<UPhysicsAssetDataflowContent>(DataflowContent))
	{
		Content->SetDataflowAsset(Instance.GetDataflowAsset());
		Content->SetDataflowTerminal(Instance.GetDataflowTerminal().ToString());

		if(UPhysicsAsset* PhysAsset = GetTypedOuter<UPhysicsAsset>())
		{
			Content->SetSkeletalMesh(PhysAsset->GetPreviewMesh());
			Content->SetPhysicsAsset(PhysAsset);
		}
	}
}

void UDataflowAttachment::ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent)
{
	
}

const FDataflowInstance& UDataflowAttachment::GetDataflowInstance() const
{
	return Instance;
}

FDataflowInstance& UDataflowAttachment::GetDataflowInstance()
{
	return Instance;
}

TObjectPtr<UDataflowBaseContent> UDataflowAttachment::CreateDataflowContent()
{
	TObjectPtr<UDataflowBaseContent> BaseContent = NewObject<UPhysicsAssetDataflowContent>();

	BaseContent->SetDataflowOwner(this);
	BaseContent->SetTerminalAsset(this);

	WriteDataflowContent(BaseContent);

	return BaseContent;
}
