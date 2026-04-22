// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVMeshBuilderSettings.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVGrowthData.h"
#include "DataTypes/PVMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVProfileFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Helpers/PVAnalyticsHelper.h"
#include "Implementations/PVMeshBuilder.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "PVMeshBuilderSettings"

#if WITH_EDITOR
FText UPVMeshBuilderSettings::GetDefaultNodeTitle() const
{ 
	return LOCTEXT("NodeTitle", "Mesh Builder"); 
}

FText UPVMeshBuilderSettings::GetNodeTooltipText() const 
{ 
	return LOCTEXT("NodeTooltip", 
		"Builds a 3D Mesh for the plant based on the structure incoming from previous nodes.\n\n"
		"Users can specify materials to be used for the mesh. By default it uses the materials specified in the Procedural Vegetation Preset. It has 3 groups of settings : \n"
		"- Material\n"
		"- Mesh\n"
		"- Displacement\n"
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}

void UPVMeshBuilderSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FPVMeshBuilderParams, DisplacementTexture))
	{
		DisplacementWarnings.Empty();
		if (MesherSettings.DisplacementTexture)
		{
			FPVMeshBuilder::ExtractDisplacementData(MesherSettings.DisplacementTexture, MesherSettings.DisplacementValues, DisplacementWarnings);
		}
		else
		{
			MesherSettings.DisplacementValues.Empty();
		}

		Modify();
	}
	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FPVMeshBuilderParams, MaterialSettings))
	{
		if (MesherSettings.bOverrideMaterial)
		{
			MesherSettings.MaterialSettings = CustomMaterialSettings;
		}
		else
		{
			MesherSettings.MaterialSettings = DefaultMaterialSettings;
		}
	}
}

void UPVMeshBuilderSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;

	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPVMeshBuilderSettings, MesherSettings))
		{
			FName Name = Property->GetFName();
			
			if(Name == "Material")
			{
				int Index = PropertyChangedEvent.GetArrayIndex("MaterialSetups");

				if (Index >= 0 && MesherSettings.MaterialSettings.MaterialSetups.Num() > Index)
				{
					FString MatPath = MesherSettings.MaterialSettings.MaterialSetups[Index].Material->GetPathName();

					PV::Analytics::SendMaterialChangeEvent(MatPath);
				}
			}
		}
	}
}

void UPVMeshBuilderSettings::PostLoad()
{
	Super::PostLoad();

	DisplacementWarnings.Empty();
	if (MesherSettings.DisplacementTexture)
	{
		FPVMeshBuilder::ExtractDisplacementData(MesherSettings.DisplacementTexture, MesherSettings.DisplacementValues, DisplacementWarnings);
	}
	else
	{
		MesherSettings.DisplacementValues.Empty();
	}
}
#endif

FPCGDataTypeIdentifier UPVMeshBuilderSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGDataTypeIdentifier UPVMeshBuilderSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGElementPtr UPVMeshBuilderSettings::CreateElement() const
{
	return MakeShared<FPVMeshBuilderElement>();
}

bool FPVMeshBuilderElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVMeshBuilderElement::Execute);

	check(InContext);

	const UPVMeshBuilderSettings* Settings = InContext->GetInputSettings<UPVMeshBuilderSettings>();
	check(Settings);

	UPVMeshBuilderSettings* MutableSettings = const_cast<UPVMeshBuilderSettings*>(Settings);
	check(MutableSettings);

	MutableSettings->MesherSettings.PlantProfileOptions.Empty();
	MutableSettings->MesherSettings.PlantProfileOptions.Add(TEXT("None"));

	const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (!Inputs.IsEmpty())
	{
		if (const UPVGrowthData* InputData = Cast<UPVGrowthData>(Inputs[0].Data))
		{
			const FManagedArrayCollection& SkeletonCollection = InputData->GetCollection();
			auto& MaterialSettings = MutableSettings->MesherSettings.MaterialSettings;
					
			if (!Settings->MesherSettings.bOverrideMaterial )
			{
				const PV::Facades::FBranchFacade BranchFacade(SkeletonCollection);
				const FString TrunkMaterial = BranchFacade.GetTrunkMaterialPath();
				const TArray<FVector2f> TrunkURanges = BranchFacade.GetTrunkURange();

				int32 Generation = 0;
				for (const FVector2f& URange : TrunkURanges)
				{
					MaterialSettings.SetMaterial(TrunkMaterial, URange, Generation);
					Generation++;
				}

				MutableSettings->DefaultMaterialSettings = MaterialSettings;

				if (!MutableSettings->bCustomMaterialSet)
				{
					MutableSettings->CustomMaterialSettings = MaterialSettings;
					MutableSettings->bCustomMaterialSet = true;
				}
			}
			else
			{
				MutableSettings->CustomMaterialSettings = MaterialSettings;
			}

			if (const PV::Facades::FPlantProfileFacade PlantProfileFacade = PV::Facades::FPlantProfileFacade(SkeletonCollection);
				PlantProfileFacade.NumProfileEntries() > 0)
			{
				for (int32 i = 0; i < PlantProfileFacade.NumProfileEntries(); i++)
				{
					MutableSettings->MesherSettings.PlantProfileOptions.Add(FString::FromInt(i));
				}

				MutableSettings->MesherSettings.bIsPlantProfileDropdownEnabled = true;
			}
			else
			{
				MutableSettings->MesherSettings.bIsPlantProfileDropdownEnabled = false;
			}

			if (!Settings->DisplacementWarnings.IsEmpty())
			{
				PCGLog::LogWarningOnGraph(FText::FromString(FString::Format(TEXT("{0} "), {*Settings->DisplacementWarnings})), InContext);
			}

			UPVMeshData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVMeshData>(InContext);

			FGeometryCollection OutGeometryCollection;
			FPVMeshBuilder::GenerateGeometryCollection(SkeletonCollection, Settings->MesherSettings, OutGeometryCollection);

			OutManagedArrayCollectionData->Initialize(MoveTemp(OutGeometryCollection));

			FPCGTaggedData& CollectionOutput = InContext->OutputData.TaggedData.Emplace_GetRef();
			CollectionOutput.Data = OutManagedArrayCollectionData;
			CollectionOutput.Pin = PCGPinConstants::DefaultOutputLabel;

			// TODO : Figure out if we want to keep the dynamic mesh inn the ProceduralVegetation data or as a pin
			// UPCGDynamicMeshData* DynamicMeshData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(InContext);
			// TObjectPtr<UDynamicMesh> OutMesh = NewObject<UDynamicMesh>();
			// FPVMeshBuilder::GenerateDynamicMesh(SkeletonCollection, OutMesh);
			// DynamicMeshData->Initialize(OutMesh, true);
			//
			// FPCGTaggedData& MeshOutput = InContext->OutputData.TaggedData.Emplace_GetRef();
			// MeshOutput.Data = DynamicMeshData;
			// MeshOutput.Pin = TEXT("Mesh");
		}
		else
		{
			PCGLog::InputOutput::LogInvalidInputDataError(InContext);
			return true;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE