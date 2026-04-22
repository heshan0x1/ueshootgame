// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDebugVisualizer.h"

#include "PVDebugVisualizationBase.h"
#include "PVDebugVisualizations.h"
#include "DataTypes/PVData.h"

FVisualizerDrawContext::FVisualizerDrawContext(const FManagedArrayCollection& InCollection, const FPVVisualizationSettings& InSettings,
	FPCGSceneSetupParams& InSceneParams)
	: Collection(InCollection)
	, VisualizationSettings(InSettings)
	, SceneSetupParams(InSceneParams)
{
}

void FPVDebugVisualizer::Draw(const UPVData* InData, FPCGSceneSetupParams& InOutParams)
{
	check(InData);
	
	TArray<FPVVisualizationSettings> VisualizationSettings = InData->GetDebugSettings().VisualizationSettings;

	for (int32 i = 0; i < VisualizationSettings.Num(); i++)
	{
		auto Settings = VisualizationSettings[i];

		FVisualizerDrawContext Context(InData->GetCollection(), Settings,InOutParams);

		if (FPVDebugVisualizationPtr VisualizationPtr = CreateVisualizer(Settings.DebugType))
		{
			VisualizationPtr->Draw(Context);
		}	
	}
}

FPVDebugVisualizationPtr FPVDebugVisualizer::CreateVisualizer(EPVDebugType InDebugType)
{
	switch (InDebugType)
	{
		case EPVDebugType::Point:
			return MakeShared<FPVPointDebugVisualization>();

		case EPVDebugType::Foliage:
			return MakeShared<FPVFoliageDebugVisualization>();

		case EPVDebugType::Branches:
			return MakeShared<FPVBranchDebugVisualization>();

		default:
			return nullptr;
	}
}
