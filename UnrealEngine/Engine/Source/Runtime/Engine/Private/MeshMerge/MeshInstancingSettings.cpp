// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMerge/MeshInstancingSettings.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshInstancingSettings)

FMeshInstancingSettings::FMeshInstancingSettings()
	: ActorClassToUse(AActor::StaticClass())
	, InstanceReplacementThreshold(2)
	, bSkipMeshesWithVertexColors(true)
	, bUseHLODVolumes(true)
	, ISMComponentToUse(UInstancedStaticMeshComponent::StaticClass())
{
}
