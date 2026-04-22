// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/PVOutputSettings.h"
#include "PCGContext.h"
#include "DataTypes/PVMeshData.h"
#include "DataTypes/PVFoliageMeshData.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Helpers/PVAnalyticsHelper.h"


#define LOCTEXT_NAMESPACE "PVOutputSettings"

#if WITH_EDITOR
FText UPVOutputSettings::GetDefaultNodeTitle() const
{
	return ExportSettings.MeshName.IsNone() 
		? LOCTEXT("NodeTitle", "Output") 
		: FText::FromName(ExportSettings.MeshName);
}

FText UPVOutputSettings::GetNodeTooltipText() const 
{ 
	return LOCTEXT("NodeTooltip", 
		"This node allows the user to save the output of the graph as:\n"
		"- Static Mesh\n"
		"- Static Mesh with Nanite Foliage enabled\n"
		"- Skeletal Mesh\n"
		"- Skeletal Mesh with Nanite Foliage enabled\n\n"

		"If the foliage meshes do not contain skeletal data and Skeletal mesh export is selected, it will build the skeletal data for foliage meshes"
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}
#endif

UPVOutputSettings::UPVOutputSettings()
{
	if (PCGHelpers::IsNewObjectAndNotDefault(this))
	{
		static FString DefaultMeshName = "Output";
		static FString DefaultAssetPath = "/Game/ProceduralVegetation/";
		static FString DefaultWindSettingsPath = "/ProceduralVegetationEditor/SampleAssets/WindSettings/DefaultTreeWindSettings.DefaultTreeWindSettings";
		ExportSettings.Initialize(DefaultAssetPath, DefaultMeshName, DefaultWindSettingsPath);
	}
}

FPCGDataTypeIdentifier UPVOutputSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier::Construct(FPVDataTypeInfoMesh::AsId(), FPVDataTypeInfoFoliageMesh::AsId());
}

TArray<FPCGPinProperties> UPVOutputSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;

	FPCGPinProperties& Pin = Properties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);
	Pin.SetRequiredPin();
	Pin.SetAllowMultipleConnections(false);
	Pin.bInvisiblePin = true;

	return Properties;
}

FPCGElementPtr UPVOutputSettings::CreateElement() const
{
	return MakeShared<FPVOutputElement>();
}

#if	WITH_EDITOR
void UPVOutputSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FPVExportParams, WindSettings))
	{
		if (ExportSettings.WindSettings)
		{
			PV::Analytics::SendWindSettingsChangeEvent(ExportSettings.WindSettings->GetPathName());
		}
	}
}

EPCGChangeType UPVOutputSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UPVOutputSettings, ExportSettings))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FDirectoryPath, Path)
			||PropertyName == GET_MEMBER_NAME_CHECKED(FPVExportParams, MeshName))
		{
			return EPCGChangeType::Cosmetic;
		}
	}

	return Super::GetChangeTypeForProperty(PropertyChangedEvent);
}

#endif

bool FPVOutputElement::ExecuteInternal(FPCGContext* InContext) const
{
	const UPVOutputSettings* Settings = InContext->GetInputSettings<UPVOutputSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if (const UPVData* InputData = Cast<UPVData>(Input.Data))
		{
			FString Error;
			if (!Settings->ExportSettings.Validate(Error))
			{
				PCGLog::LogErrorOnGraph(FText::FromString(Error), InContext);
				return true;
			}

			InContext->OutputData = InContext->InputData;
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
