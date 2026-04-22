// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_MidiStutterSequence.h"
#include "HarmonixMetasound/DataTypes/MidiStutterSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_MidiStutterSequence)

TSoftClassPtr<UObject> UAssetDefinition_MidiStutterSequence::GetAssetClass() const
{
	return UMidiStutterSequence::StaticClass();
}

FText UAssetDefinition_MidiStutterSequence::GetAssetDisplayName() const
{
	return NSLOCTEXT("AssetTypeActions", "MIDIStutterSequenceDefinition", "MIDI Stutter Sequence");
}

FLinearColor  UAssetDefinition_MidiStutterSequence::GetAssetColor() const
{
	return FLinearColor(1.0f, 0.5f, 0.0f);
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MidiStutterSequence::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::Audio / NSLOCTEXT("Harmonix", "HmxAssetCategoryName", "Harmonix") };
	return Categories;
}

bool UAssetDefinition_MidiStutterSequence::CanImport() const
{
	return false;
}

