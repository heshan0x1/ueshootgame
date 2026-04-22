// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshLODContainerNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeMeshLODContainerNode)

UInterchangeMeshLODContainerNode::UInterchangeMeshLODContainerNode()
{
	LODMeshUids.Initialize(Attributes.ToSharedRef(), TEXT("__LODMeshUids__Key"));
}

FString UInterchangeMeshLODContainerNode::GetTypeName() const
{
	const FString TypeName = TEXT("LODContainerComponentNode");
	return TypeName;
}

bool UInterchangeMeshLODContainerNode::AddMeshLODNodeUid(const FString& MeshLODNodeUid)
{
	return LODMeshUids.AddItem(MeshLODNodeUid);
}

void UInterchangeMeshLODContainerNode::GetMeshLODNodeUids(TArray<FString>& OutMeshLODNodeUid) const
{
	LODMeshUids.GetItems(OutMeshLODNodeUid);
}

bool UInterchangeMeshLODContainerNode::RemoveMeshLODNodeUid(const FString& MeshLODNodeUid)
{
	return LODMeshUids.RemoveItem(MeshLODNodeUid);
}

bool UInterchangeMeshLODContainerNode::ResetMeshLODNodeUids()
{
	return LODMeshUids.RemoveAllItems();
}