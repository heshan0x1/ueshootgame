// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVCarveSettings.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVGrowthData.h"
#include "Data/PCGBasePointData.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGSettingsHelpers.h"

#define LOCTEXT_NAMESPACE "PVCarveSettings"

#if WITH_EDITOR
FText UPVCarveSettings::GetDefaultNodeTitle() const
{ 
	return LOCTEXT("NodeTitle", "Carve"); 
}

FText UPVCarveSettings::GetNodeTooltipText() const
{ 
	return LOCTEXT("NodeTooltip", 
		"Allows the user to trim the vegetation structure.The Carve Basis determines the reference used for the cut. \n\n"
		"LengthFromRoot – distance measured from the plant’s root / trunk base.\n"
		"From Bottom – relative height starting from the plant’s lowest point.\n"
		"ZPosition – absolute world - space Z axis position.\n"
		"Radius – Thickness of branch"
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}
#endif

FPCGDataTypeIdentifier UPVCarveSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGDataTypeIdentifier UPVCarveSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGElementPtr UPVCarveSettings::CreateElement() const
{
	return MakeShared<FPVCarveElement>();
}

bool FPVCarveElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVCarveElement::ExecuteInternal);

	check(InContext);

	const UPVCarveSettings* InputSettings = InContext->GetInputSettings<UPVCarveSettings>();
	check(InputSettings);

	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if (const UPVGrowthData* InputData = Cast<UPVGrowthData>(Input.Data))
		{
			FManagedArrayCollection SourceCollection = InputData->GetCollection();

			FManagedArrayCollection OutCollection;
			SourceCollection.CopyTo(&OutCollection);

			UPVGrowthData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVGrowthData>(InContext);

			auto [CarveBasis, Carve] = InputSettings->CarveSettings;
			FPVCarve::ApplyCarve(OutCollection, SourceCollection, CarveBasis, Carve);

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