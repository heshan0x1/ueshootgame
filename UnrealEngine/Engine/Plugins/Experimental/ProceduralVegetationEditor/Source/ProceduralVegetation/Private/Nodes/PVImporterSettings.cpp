// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVImporterSettings.h"

#include "ProceduralVegetationModule.h"

#include "DataTypes/PVData.h"
#include "DataTypes/PVGrowthData.h"

#include "GeometryCollection/ManagedArrayCollection.h"

#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Helpers/PVFoliageJSONHelper.h"
#include "Helpers/PVJSONHelper.h"

UPVImporterSettings::UPVImporterSettings()
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		bExposeToLibrary = PV::Utilities::DebugModeEnabled();
	}
#endif
}

TArray<FPCGPinProperties> UPVImporterSettings::InputPinProperties() const
{
	return TArray<FPCGPinProperties>();
}

FPCGElementPtr UPVImporterSettings::CreateElement() const
{
	return MakeShared<FPVImporterElement>();
}

FPCGDataTypeIdentifier UPVImporterSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

bool FPVImporterElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVImporterElement::Execute);

	check(InContext);

	const UPVImporterSettings* Settings = InContext->GetInputSettings<UPVImporterSettings>();
	check(Settings);
	
	FString SkeletonFilePath = Settings->SkeletonFile.FilePath;
	if (SkeletonFilePath.StartsWith("Content"))
	{
		SkeletonFilePath = FPaths::ProjectDir() / SkeletonFilePath;
	}
	FString ErrorMessage;

	FManagedArrayCollection Collection;
	bool bSuccess = PV::LoadMegaPlantsJsonToCollection(Collection, SkeletonFilePath, ErrorMessage);
	if (!bSuccess)
	{
		PCGLog::LogErrorOnGraph(FText::FromString(ErrorMessage), InContext);
		return true;
	}

	if (!PV::Utilities::IsValidPVData(Collection))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Format(TEXT("Invalid data in the imported json {0}"), { SkeletonFilePath})), InContext);
		return true;
	}

	PV::PVFoliageJSONHelper::SetFoliagePaths(Collection, SkeletonFilePath);
	
	UPVGrowthData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVGrowthData>(InContext);
	OutManagedArrayCollectionData->Initialize(MoveTemp(Collection));
		
	InContext->OutputData.TaggedData.Emplace(OutManagedArrayCollectionData);
	
	return true;
}
