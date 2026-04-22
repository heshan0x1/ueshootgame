// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshapeCommon.h"

#include "CustomizableObjectNodeMeshMorph.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FMeshReshapeBoneReference;
enum class EBoneDeformSelectionMethod : uint8;

UCLASS(MinimalAPI)
class UCustomizableObjectNodeMeshMorph : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UE_API UCustomizableObjectNodeMeshMorph();

	// Begin EdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FLinearColor GetNodeTitleColor() const override;
	UE_API virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual bool IsNodeOutDatedAndNeedsRefresh() override;
	UE_API virtual FString GetRefreshMessage() const override;
	UE_API virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;


	// Own interface
	UE_API class UCustomizableObjectNodeSkeletalMesh* GetSourceSkeletalMesh() const;

	UEdGraphPin* MeshPin() const
	{
		return FindPin(TEXT("Mesh"), EGPD_Input);
	}

	UEdGraphPin* FactorPin() const
	{
		return FindPin(TEXT("Factor"));
	}

	UE_API UEdGraphPin* MorphTargetNamePin() const;

	UE_API void Serialize(FArchive& Ar) override;

	UPROPERTY(Category=MeshMorph, EditAnywhere)
	FString MorphTargetName;

	/** Experimental - Enable the deformation of the skeleton of the base mesh. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bReshapeSkeleton = false;

	/** Experimental - Enable the deformation of physics volumes of the base mesh */
    UPROPERTY(EditAnywhere, Category = CustomizableObject)
    bool bReshapePhysicsVolumes = false;

	/** Bone Reshape Selection Method */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta = (EditCondition = "bReshapeSkeleton"))
	EBoneDeformSelectionMethod SelectionMethod;
	
	/** Array with selected bones that will be deformed */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta = (EditCondition = "bReshapeSkeleton && (SelectionMethod == EBoneDeformSelectionMethod::ONLY_SELECTED || SelectionMethod == EBoneDeformSelectionMethod::ALL_BUT_SELECTED)"))
	TArray<FMeshReshapeBoneReference> BonesToDeform;

	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta = (DisplayName = "Selection Method", EditCondition = "bReshapePhysicsVolumes"))
	EBoneDeformSelectionMethod PhysicsSelectionMethod;

	/** Array with bones with physics bodies that will be deformed */
	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta = (EditCondition = "bReshapePhysicsVolumes && (PhysicsSelectionMethod == EBoneDeformSelectionMethod::ONLY_SELECTED || PhysicsSelectionMethod == EBoneDeformSelectionMethod::ALL_BUT_SELECTED)"))
	TArray<FMeshReshapeBoneReference> PhysicsBodiesToDeform;

	UPROPERTY()
	bool bDeformAllBones_DEPRECATED = false;

	UPROPERTY()
	bool bDeformAllPhysicsBodies_DEPRECATED = false;

private:

	UPROPERTY()
	FEdGraphPinReference MorphTargetNamePinRef;

};

#undef UE_API
