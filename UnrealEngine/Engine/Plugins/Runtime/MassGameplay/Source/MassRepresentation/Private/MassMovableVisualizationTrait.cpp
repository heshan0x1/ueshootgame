// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassMovableVisualizationTrait.h"
#include "MassEntityTemplateRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassMovableVisualizationTrait)


void UMassMovableVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	if (!bAllowServerSideVisualization && World.IsNetMode(NM_DedicatedServer)
		&& !BuildContext.IsInspectingData())
	{
		return;
	}

	for (FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc : StaticMeshInstanceDesc.Meshes)
	{
		MeshDesc.Mobility = EComponentMobility::Movable;
	}

	Super::BuildTemplate(BuildContext, World);
}
