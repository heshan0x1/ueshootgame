// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "PCGGraph.h"
#include "ProceduralVegetation.generated.h"

class UProceduralVegetationPreset;

UCLASS(MinimalAPI)
class UProceduralVegetationGraph : public UPCGGraph
{
	GENERATED_BODY()

public:
	UProceduralVegetationGraph(const FObjectInitializer& ObjectInitializer)
		: UPCGGraph(ObjectInitializer)
	{
#if WITH_EDITORONLY_DATA
		bIsStandaloneGraph = false;
		bExposeGenerationInAssetExplorer = false;
#endif
		
#if WITH_EDITOR
		// Hides input and output nodes
		SetHiddenFlagInputNode(/*bHidden=*/true);
		SetHiddenFlagOutputNode(/*bHidden=*/true);
#endif
	}

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual bool ShouldDisplayDebuggingProperties() const override { return false; }
	virtual bool CanToggleStandaloneGraph() const override { return false; };
	virtual bool IsExportToLibraryEnabled() const override { return false; }
	virtual bool ShowGraphCustomization() const override { return false; }
	virtual bool IsTemplatePropertyEnabled() const override { return false; }
#endif
};

/**
 * Asset type for procedural plant generation
 */
UCLASS()
class PROCEDURALVEGETATION_API UProceduralVegetation : public UObject
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UProceduralVegetationGraph> Graph;

public:
	TObjectPtr<UProceduralVegetationGraph>& GetGraph() { return Graph; }
	const TObjectPtr<UProceduralVegetationGraph>& GetGraph() const { return Graph; }

	void CreateGraph(const UProceduralVegetationGraph* InGraph = nullptr);

#if WITH_EDITOR
	void CreateGraphFromPreset(const TObjectPtr<UProceduralVegetationPreset> InPreset);
#endif
};
