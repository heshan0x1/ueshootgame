// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_MetaHumanFaceAnimationSolver.h"
#include "MetaHumanFaceAnimationSolver.h"
#include "MetaHumanCoreEditorModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_MetaHumanFaceAnimationSolver)

FText UAssetDefinition_MetaHumanFaceAnimationSolver::GetAssetDisplayName() const
{
	return NSLOCTEXT("MetaHuman", "MetaHumanFaceAnimationSolverAssetName", "Face Animation Solver");
}

FLinearColor UAssetDefinition_MetaHumanFaceAnimationSolver::GetAssetColor() const
{
	return FColor::Orange;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaHumanFaceAnimationSolver::GetAssetClass() const
{
	return UMetaHumanFaceAnimationSolver::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaHumanFaceAnimationSolver::GetAssetCategories() const
{
	return FModuleManager::GetModuleChecked<IMetaHumanCoreEditorModule>(TEXT("MetaHumanCoreEditor")).GetMetaHumanAdvancedAssetCategoryPath();
}
