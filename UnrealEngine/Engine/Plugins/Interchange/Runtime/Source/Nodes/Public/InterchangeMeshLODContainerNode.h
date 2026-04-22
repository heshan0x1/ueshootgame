// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeMeshLODContainerNode.generated.h"

#define UE_API INTERCHANGENODES_API

UCLASS(MinimalAPI, BlueprintType)
class UInterchangeMeshLODContainerNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:

	UE_API UInterchangeMeshLODContainerNode();

	UE_API virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Scene | Component | LODContainer")
	UE_API bool AddMeshLODNodeUid(const FString& MeshLODNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Scene | Component | LODContainer")
	UE_API void GetMeshLODNodeUids(TArray<FString>& OutMeshLODNodeUid) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Scene | Component | LODContainer")
	UE_API bool RemoveMeshLODNodeUid(const FString& MeshLODNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Scene | Component | LODContainer")
	UE_API bool ResetMeshLODNodeUids();

private:
	UE::Interchange::TArrayAttributeHelper<FString> LODMeshUids;
};

#undef UE_API
