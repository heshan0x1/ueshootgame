// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeComponent.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByNameDefaultPin.h"
#include "SGraphNode.h"
#include "GameplayTagContainer.h"

#include "CustomizableObjectNodeComponentPassthroughMesh.generated.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class FAssetThumbnail;
class FAssetThumbnailPool;
class FSkeletalMeshModel;
class ISinglePropertyView;
class UAnimInstance;
class UCustomizableObjectLayout;
class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeRemapPinsByName;
class UMaterialInterface;
class UObject;
class USkeletalMesh;
class UTexture2D;
struct FPropertyChangedEvent;
struct FSkeletalMaterial;


/** PinData of a pin that belongs to a Skeletal Mesh Section. */
UCLASS()
class UCustomizableObjectNodeComponentMeshPinDataSection : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:
	void Init(int32 InLODIndex, int32 InSectionIndex);

	int32 GetLODIndex() const;

	int32 GetSectionIndex() const;

protected:
	virtual bool Equals(const UCustomizableObjectNodePinData& Other) const override;

private:
	UPROPERTY()
	int32 LODIndex = -1;

	UPROPERTY()
	int32 SectionIndex = -1;
};


/** PinData of a Mesh pin. */
UCLASS()
class UCustomizableObjectNodeComponentMeshPinDataMaterial : public UCustomizableObjectNodeComponentMeshPinDataSection
{
	GENERATED_BODY()
};


UCLASS(MinimalAPI)
class UCustomizableObjectNodeComponentPassthroughMesh : public UCustomizableObjectNodeComponent
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = PassthroughMesh)
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	// UObject interface.
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	
	// UEdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	UE_API virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	UE_API virtual bool IsExperimental() const override;

	// Own interface
	
	UE_API void GetPinSection(const UEdGraphPin& Pin, int32& OutLODIndex, int32& OutSectionIndex) const;

	/** Find the pin for a given lod and section. */
	UE_API UEdGraphPin* GetMaterialPin(const int32 LODIndex, const int32 SectionIndex) const;

	/** Returns the material associated to the given output pin. */
	UE_API UMaterialInterface* GetMaterialFor(const UEdGraphPin* Pin) const;
	UE_API FSkeletalMaterial* GetSkeletalMaterialFor(const UEdGraphPin& Pin) const;

private:
	UE_API UMaterialInterface* GetMaterialInterfaceFor(const int32 LODIndex, const int32 MaterialIndex, const FSkeletalMeshModel* ImportedModel = nullptr) const;
	UE_API FSkeletalMaterial* GetSkeletalMaterialFor(const int32 LODIndex, const int32 MaterialIndex, const FSkeletalMeshModel* ImportedModel = nullptr) const;

	static UE_API const FName OutputPinName;

};

#undef UE_API
