// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeComponentPassthroughMesh.h"

#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCO/LoadUtils.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/Nodes/CustomizableObjectNodeLayoutBlocks.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeComponentPassthroughMesh)

class UCustomizableObjectNodeRemapPinsByName;
class UObject;


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

const FName UCustomizableObjectNodeComponentPassthroughMesh::OutputPinName(TEXT("Component"));


void UCustomizableObjectNodeComponentMeshPinDataSection::Init(int32 InLODIndex, int32 InSectionIndex)
{
	LODIndex = InLODIndex;
	SectionIndex = InSectionIndex;
}


int32 UCustomizableObjectNodeComponentMeshPinDataSection::GetLODIndex() const
{
	return LODIndex;
}


int32 UCustomizableObjectNodeComponentMeshPinDataSection::GetSectionIndex() const
{
	return SectionIndex;
}


bool UCustomizableObjectNodeComponentMeshPinDataSection::Equals(const UCustomizableObjectNodePinData& Other) const
{
	if (GetClass() != Other.GetClass())
	{
		return false;
	}

	const UCustomizableObjectNodeComponentMeshPinDataSection& OtherTyped = static_cast<const UCustomizableObjectNodeComponentMeshPinDataSection&>(Other);
	if (LODIndex != OtherTyped.LODIndex ||
		SectionIndex != OtherTyped.SectionIndex)
	{
		return false;
	}

	return Super::Equals(Other);
}


void UCustomizableObjectNodeComponentPassthroughMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged && PropertyThatChanged->GetName() == GET_MEMBER_NAME_CHECKED(UCustomizableObjectNodeComponentPassthroughMesh, SkeletalMesh))
	{
		ReconstructNode();
	}
}


void UCustomizableObjectNodeComponentPassthroughMesh::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::ChangedComponentsInheritance)
	{
		OutputPin = FindPin(OutputPinName, EGPD_Output);
	}
}


void UCustomizableObjectNodeComponentPassthroughMesh::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	Super::AllocateDefaultPins(RemapPins);

	if (SkeletalMesh.IsNull())
	{
		return;
	}
	
	// Support SkeletalMeshes only, for now.
	USkeletalMesh* StrongSkeletalMesh = UE::Mutable::Private::LoadObject<USkeletalMesh>(SkeletalMesh);
	if (!StrongSkeletalMesh)
	{
		return;
	}
	
	if (const FSkeletalMeshModel* ImportedModel = StrongSkeletalMesh->GetImportedModel())
	{
		const int32 NumLODs = StrongSkeletalMesh->GetLODNum();
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			const int32 NumSections = ImportedModel->LODModels[LODIndex].Sections.Num();
			for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
			{
				// Ignore disabled sections.
				const bool bIsSectionDisabled = ImportedModel ->LODModels[LODIndex].Sections[SectionIndex].bDisabled;
				if (bIsSectionDisabled)
				{
					continue;
				}

				UMaterialInterface* MaterialInterface = GetMaterialInterfaceFor(LODIndex, SectionIndex, ImportedModel);
				
				// Material for the section
				{
					UCustomizableObjectNodeComponentMeshPinDataMaterial* PinData = NewObject<UCustomizableObjectNodeComponentMeshPinDataMaterial>(this);
					PinData->Init(LODIndex, SectionIndex);
					
					FString SectionFriendlyName = MaterialInterface ? MaterialInterface->GetName() : FString::Printf(TEXT("Section %i"), SectionIndex);
					FString MaterialPinName = FString::Printf(TEXT("LOD %i - Section %i - Material"), LODIndex, SectionIndex);

					UEdGraphPin* Pin = CustomCreatePin(EGPD_Input, UEdGraphSchema_CustomizableObject::PC_MeshSection, FName(*MaterialPinName), PinData);
					Pin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("LOD %i - Section %i - %s"), LODIndex, SectionIndex, *SectionFriendlyName));
					Pin->PinToolTip = MaterialPinName;
				}
				
			}
		}
	}
}


bool UCustomizableObjectNodeComponentPassthroughMesh::IsExperimental() const
{
	return true;
}


FText UCustomizableObjectNodeComponentPassthroughMesh::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ComponentPassthroughMesh", "Passthrough Skeletal Mesh Component");
}


void UCustomizableObjectNodeComponentPassthroughMesh::GetPinSection(const UEdGraphPin& Pin, int32& OutLODIndex, int32& OutSectionIndex) const
{
	if (const UCustomizableObjectNodeComponentMeshPinDataSection* PinData = Cast<UCustomizableObjectNodeComponentMeshPinDataSection>(GetPinData(Pin)))
	{
		OutLODIndex = PinData->GetLODIndex();
		OutSectionIndex = PinData->GetSectionIndex();
		return;
	}

	OutLODIndex = -1;
	OutSectionIndex = -1;
}


UMaterialInterface* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialFor(const UEdGraphPin* Pin) const
{
	USkeletalMesh* StrongSkeletalMesh = UE::Mutable::Private::LoadObject<USkeletalMesh>(SkeletalMesh);
	if (StrongSkeletalMesh)
	{
		if (FSkeletalMaterial* SkeletalMaterial = GetSkeletalMaterialFor(*Pin))
		{
			return SkeletalMaterial->MaterialInterface;
		}
	}

	return nullptr;
}


FSkeletalMaterial* UCustomizableObjectNodeComponentPassthroughMesh::GetSkeletalMaterialFor(const UEdGraphPin& Pin) const
{
	int32 LODIndex;
	int32 SectionIndex;
	GetPinSection(Pin, LODIndex, SectionIndex);

	return GetSkeletalMaterialFor(LODIndex, SectionIndex);
}


FText UCustomizableObjectNodeComponentPassthroughMesh::GetTooltipText() const
{
	return LOCTEXT("ComponentPassthroughMesh_Tooltip", "Define a new object component based on a Skeletal Mesh.");
}


UMaterialInterface* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialInterfaceFor(const int32 LODIndex, const int32 SectionIndex, const FSkeletalMeshModel* ImportedModel) const
{
	if (FSkeletalMaterial* SkeletalMaterial = GetSkeletalMaterialFor(LODIndex, SectionIndex, ImportedModel))
	{
		return SkeletalMaterial->MaterialInterface;
	}

	return nullptr;
}


FSkeletalMaterial* UCustomizableObjectNodeComponentPassthroughMesh::GetSkeletalMaterialFor(const int32 LODIndex, const int32 SectionIndex, const FSkeletalMeshModel* ImportedModel) const
{
	USkeletalMesh* StrongSkeletalMesh = Cast<USkeletalMesh>(UE::Mutable::Private::LoadObject(SkeletalMesh));
	if (!StrongSkeletalMesh)
	{
		return nullptr;
	}

	// We assume that LODIndex and MaterialIndex are valid for the imported model
	int32 SkeletalMeshMaterialIndex = INDEX_NONE;
	
	// Check if we have lod info map to get the correct material index
	if (const FSkeletalMeshLODInfo* LodInfo = StrongSkeletalMesh->GetLODInfo(LODIndex))
	{
		if (LodInfo->LODMaterialMap.IsValidIndex(SectionIndex))
		{
			SkeletalMeshMaterialIndex = LodInfo->LODMaterialMap[SectionIndex];
		}
	}

	// Only deduce index when the explicit mapping is not found or there is no remap
	if (SkeletalMeshMaterialIndex == INDEX_NONE)
	{
	if (ImportedModel && ImportedModel->LODModels.IsValidIndex(LODIndex) && ImportedModel->LODModels[LODIndex].Sections.IsValidIndex(SectionIndex))
	{
		SkeletalMeshMaterialIndex = ImportedModel->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex;
	}
	else
	{
		FSkeletalMeshModel* AuxImportedModel = StrongSkeletalMesh->GetImportedModel();

		if (AuxImportedModel)
		{
			if (AuxImportedModel->LODModels.IsValidIndex(LODIndex) && AuxImportedModel->LODModels[LODIndex].Sections.IsValidIndex(SectionIndex))
			{
				SkeletalMeshMaterialIndex = AuxImportedModel->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex;
			}
		}
	}
	}
	
	if (StrongSkeletalMesh->GetMaterials().IsValidIndex(SkeletalMeshMaterialIndex))
	{
		return &StrongSkeletalMesh->GetMaterials()[SkeletalMeshMaterialIndex];
	}
	
	return nullptr;
}



UEdGraphPin* UCustomizableObjectNodeComponentPassthroughMesh::GetMaterialPin(const int32 LODIndex, const int32 SectionIndex) const
{
	for (UEdGraphPin* Pin : GetAllNonOrphanPins())
	{
		if (const UCustomizableObjectNodeComponentMeshPinDataSection* PinData = Cast<UCustomizableObjectNodeComponentMeshPinDataSection>(GetPinData(*Pin)))
		{
			if (PinData->GetLODIndex() == LODIndex &&
				PinData->GetSectionIndex() == SectionIndex)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE
