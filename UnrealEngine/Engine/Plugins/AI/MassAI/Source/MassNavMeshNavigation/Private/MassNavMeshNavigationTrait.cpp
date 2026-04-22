// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassNavMeshNavigationTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "MassNavigationFragments.h"
#include "MassNavMeshNavigationFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassNavMeshNavigationTrait)

void UMassNavMeshNavigationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireFragment<FMassMoveTargetFragment>();

	BuildContext.AddFragment<FMassNavMeshCachedPathFragment>();
	BuildContext.AddFragment<FMassNavMeshShortPathFragment>();

	BuildContext.AddFragment<FMassNavMeshBoundaryFragment>();	
}
