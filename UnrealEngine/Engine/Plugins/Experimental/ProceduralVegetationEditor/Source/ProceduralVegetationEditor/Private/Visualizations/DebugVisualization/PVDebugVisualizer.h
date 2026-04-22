// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Helpers/PVUtilities.h"

#include "PVDebugVisualizationBase.h"

struct FManagedArrayCollection;
struct FPVVisualizationSettings;
class FPVDebugVisualizationBase;
struct FPCGSceneSetupParams;
class UPVData;
enum class EPVDebugType;

struct FVisualizerDrawContext
{
	FVisualizerDrawContext(const FManagedArrayCollection& InCollection, const FPVVisualizationSettings& InSettings, FPCGSceneSetupParams& InSceneParams);
	const FManagedArrayCollection& Collection;
	const FPVVisualizationSettings& VisualizationSettings;
	FPCGSceneSetupParams& SceneSetupParams;
};

class FPVDebugVisualizer
{
public:
	static void Draw(const UPVData* InData,FPCGSceneSetupParams& InOutParams);
private:
	static FPVDebugVisualizationPtr CreateVisualizer(EPVDebugType InDebugType);
};