// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "TransformTypes.h"

#define UE_API MODELINGOPERATORSEDITORONLY_API

struct FMeshDescription;

namespace UE
{
namespace Geometry
{


class FVoxelMergeMeshesOp : public FDynamicMeshOperator
{
public:
	virtual ~FVoxelMergeMeshesOp() {}

	// inputs
	TArray<TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe>> Meshes;
	TArray<FTransformSRT3d> Transforms; // 1:1 with Meshes

	int32 VoxelCount = 128;
	double VoxelSizeD = 1.0;
	double AdaptivityD = 0;
	double IsoSurfaceD = 0;
	bool bAutoSimplify = false;

	double FastCollapseVoxelSizeMultipler = 1.2;
	int NumFastCollapsePasses = 10;

	//
	// FDynamicMeshOperator implementation
	// 

	UE_API virtual void CalculateResult(FProgressCancel* Progress) override;

private:

	// compute the voxel size based on the voxel count and the input geometry
	UE_API float ComputeVoxelSize() const;
};


} // end namespace UE::Geometry
} // end namespace UE

#undef UE_API
