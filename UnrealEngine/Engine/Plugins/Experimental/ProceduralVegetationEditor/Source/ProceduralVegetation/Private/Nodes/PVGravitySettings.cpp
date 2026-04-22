// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVGravitySettings.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVGrowthData.h"
#include "Data/PCGBasePointData.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Implementations/PVGravity.h"

#define LOCTEXT_NAMESPACE "PVGravitySettings"

#if WITH_EDITOR
FText UPVGravitySettings::GetDefaultNodeTitle() const
{ 
	return LOCTEXT("NodeTitle", "Gravity"); 
}

FText UPVGravitySettings::GetNodeTooltipText() const
{ 
	return LOCTEXT("NodeTooltip", 
		"This node controls how generated vegetation responds to directional forces, mimicking natural phenomena such as the downward pull of gravity and the growth tendency toward the direction of the optimal light conditions.(phototropism).\n\n"

		"Gravity Mode: Defines the behavior — either Gravity (branches bend downward under weight) or Phototropic (branches orient toward light).\n"
		"Strength Controls: Parameters adjust how strongly branches respond, allowing subtle or dramatic curvature.\n"
		"Direction Vector: When in gravity mode, you can set the exact vector (e.g., world down, custom direction).\n"
		"Angle Correction & Bias: Additional properties fine-tune how branches preserve their initial angles or bias between optimal light direction/shadow avoidance.\n"
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}
#endif

FPCGDataTypeIdentifier UPVGravitySettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGDataTypeIdentifier UPVGravitySettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGElementPtr UPVGravitySettings::CreateElement() const
{
	return MakeShared<FPVGravityElement>();
}

bool FPVGravityElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVGravityElement::Execute);

	check(InContext);

	const UPVGravitySettings* Settings = InContext->GetInputSettings<UPVGravitySettings>();
	check(Settings);
	
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if(const UPVGrowthData* InputData = Cast<UPVGrowthData>(Input.Data))
		{
			FManagedArrayCollection OutCollection = InputData->GetCollection();
		
			UPVGrowthData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVGrowthData>(InContext);
			
			if (Settings->GravitySettings.Gravity != 0.0f)
			{
				FPVGravity::ApplyGravity(Settings->GravitySettings, OutCollection);
			}

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