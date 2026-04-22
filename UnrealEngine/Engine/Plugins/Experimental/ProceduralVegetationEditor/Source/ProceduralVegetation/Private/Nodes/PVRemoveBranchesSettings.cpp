// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVRemoveBranchesSettings.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVGrowthData.h"
#include "Data/PCGBasePointData.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Implementations/PVRemoveBranches.h"

#define LOCTEXT_NAMESPACE "PVRemoveBranchesSettings"

#if WITH_EDITOR
FText UPVRemoveBranchesSettings::GetDefaultNodeTitle() const
{ 
	return LOCTEXT("NodeTitle", "Remove Branches");
}

FText UPVRemoveBranchesSettings::GetNodeTooltipText() const
{ 
	return LOCTEXT("NodeTooltip",
		"Removes certain branches on the basis of Length, Radius, Light (How exposed a branch is to the light), Age and Generation"
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}
#endif

FPCGDataTypeIdentifier UPVRemoveBranchesSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGDataTypeIdentifier UPVRemoveBranchesSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGElementPtr UPVRemoveBranchesSettings::CreateElement() const
{
	return MakeShared<FPVRemoveBranchesElement>();
}

bool FPVRemoveBranchesElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVRemoveBranchesElement::Execute);

	check(InContext);

	const UPVRemoveBranchesSettings* Settings = InContext->GetInputSettings<UPVRemoveBranchesSettings>();
	check(Settings);
	
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if(const UPVGrowthData* InputData = Cast<UPVGrowthData>(Input.Data))
		{
			FManagedArrayCollection OutCollection = InputData->GetCollection();
		
			UPVGrowthData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVGrowthData>(InContext);
			
			FPVRemoveBranches::ApplyRemoveBranches(Settings->BranchRemoveBasis, Settings->Threshold, OutCollection);
			
			OutManagedArrayCollectionData->Initialize(MoveTemp(OutCollection));
			InContext->OutputData.TaggedData.Emplace(OutManagedArrayCollectionData);
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