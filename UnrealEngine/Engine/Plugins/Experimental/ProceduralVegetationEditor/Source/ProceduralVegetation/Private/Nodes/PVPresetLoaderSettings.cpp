// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVPresetLoaderSettings.h"
#include "PCGContext.h"
#include "ProceduralVegetationModule.h"
#include "DataTypes/PVGrowthData.h"
#include "ProceduralVegetationPreset.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVPointFacade.h"

#define LOCTEXT_NAMESPACE "PVPresetLoaderSettings"

#if WITH_EDITOR
FText UPVPresetLoaderSettings::GetDefaultNodeTitle() const
{ 
	return LOCTEXT("NodeTitle", "Procedural Vegetation Preset Loader"); 
}

FText UPVPresetLoaderSettings::GetNodeTooltipText() const
{	
	return LOCTEXT("NodeTooltip", 
		"This allows the user to load a procedural vegetation preset. "
		"A procedural vegatation preset contains data for a specie of vegetaion. "
		"It contains hormonal information, the main structure of the vegetation, data about the branches, where foliage will / can be attached, references to foliage meshes, materials to be applied"
		"\n\nPress Ctrl + L to lock/unlock node output"
	);
}

void UPVPresetLoaderSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPVPresetLoaderSettings, Preset))
	{
		FillPresetVariationsInfo();
	}
}
#endif

void UPVPresetLoaderSettings::PostLoad()
{
	Super::PostLoad();
	if (PresetVariations.IsEmpty())
	{
		FillPresetVariationsInfo();
	}
}

TArray<FPCGPinProperties> UPVPresetLoaderSettings::InputPinProperties() const
{
	return TArray<FPCGPinProperties>();
}

TArray<FPCGPinProperties> UPVPresetLoaderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	if (Preset)
	{
		for (const auto& [VariantName, VariantData] : Preset->Variants)
		{
			if (PV::Utilities::IsValidPVData(VariantData))
			{
				FPCGPinProperties& Pin = Properties.Emplace_GetRef(*VariantName, GetOutputPinTypeIdentifier());
				Pin.bAllowMultipleData = false;
			}
		}
	}

	return Properties;
}

FPCGDataTypeIdentifier UPVPresetLoaderSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoGrowth::AsId() };
}

FPCGElementPtr UPVPresetLoaderSettings::CreateElement() const
{
	return MakeShared<FPVPresetLoaderElement>();
}

void UPVPresetLoaderSettings::FillPresetVariationsInfo()
{
	PresetVariations.Empty();

	if (!Preset)
	{
		UE_LOG(LogProceduralVegetation, Log, TEXT("Preset is null, no preset info can be filled."));
		return;
	}

	if (Preset->PresetVariations.IsEmpty())
	{
		Preset->FillVariationInfo();
	}

	PresetVariations = Preset->PresetVariations;
}

bool FPVPresetLoaderElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVPresetLoaderElement::ExecuteInternal);

	check(InContext);

	const UPVPresetLoaderSettings* Settings = InContext->GetInputSettings<UPVPresetLoaderSettings>();
	check(Settings);

	if (!Settings->Preset)
	{
		PCGLog::LogWarningOnGraph(NSLOCTEXT("PVPresetLoaderSettings", "MissingDataAsset", "Preset data asset is not set"), InContext);
		return true;
	}

	bool bValidDataExist = false;
	for (const auto& [VariantName, VariantData] : Settings->Preset->Variants)
	{
		if (PV::Utilities::IsValidPVData(VariantData))
		{
			UPVGrowthData* OutVariantData = FPCGContext::NewObject_AnyThread<UPVGrowthData>(InContext);
			OutVariantData->Initialize(CopyTemp(VariantData));

			FPCGTaggedData& CollectionOutput = InContext->OutputData.TaggedData.Emplace_GetRef();
			CollectionOutput.Data = OutVariantData;
			CollectionOutput.Pin = *VariantName;
			bValidDataExist = true;
		}
		else
		{
			PCGLog::LogWarningOnGraph(FText::FromString(FString::Format(TEXT("Skipping variation {0} due to invalid data."), {*VariantName})), InContext);
		}
	}

	if (!bValidDataExist)
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Format(TEXT("No valid variation found in data asset {0}"), { Settings->Preset.GetFName().ToString()})), InContext);
		return true;
	}
	
	return true;
}

#undef LOCTEXT_NAMESPACE