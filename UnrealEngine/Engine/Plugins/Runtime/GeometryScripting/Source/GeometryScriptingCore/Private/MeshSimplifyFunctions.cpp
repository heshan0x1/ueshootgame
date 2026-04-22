// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshSimplifyFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"
#include "Polygroups/PolygroupSet.h"
#include "GroupTopology.h"
#include "Operations/PolygroupRemesh.h"
#include "Operations/MeshClusterSimplifier.h"
#include "ConstrainedDelaunay2.h"

#if WITH_EDITOR
// for UE Standard simplifier (editor only)
#include "CleaningOps/SimplifyMeshOp.h"
#include "StaticMeshAttributes.h"
#include "Modules/ModuleManager.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"
#endif

#include "UDynamicMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshSimplifyFunctions)

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshSimplifyFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPlanar(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPlanarSimplifyOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToPlanar_InvalidInput", "ApplySimplifyToPlanar: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FQEMSimplification Simplifier(&EditMesh);

		// todo: set up seam collapse etc?

		Simplifier.CollapseMode = FQEMSimplification::ESimplificationCollapseModes::AverageVertexPosition;
		Simplifier.SimplifyToMinimalPlanar( FMath::Max(0.00001, Options.AngleThreshold) );

		if (Options.bAutoCompact)
		{
			EditMesh.CompactInPlace();
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPolygroupTopology(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPolygroupSimplifyOptions Options,
	FGeometryScriptGroupLayer GroupLayer,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToPolygroupTopology_InvalidInput", "ApplySimplifyToPolygroupTopology: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FPolygroupLayer InputGroupLayer{ GroupLayer.bDefaultLayer, GroupLayer.ExtendedLayerIndex };
		if (InputGroupLayer.CheckExists(&EditMesh) == false)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToPolygroupTopology_MissingGroups", "ApplySimplifyToPolygroupTopology: Target Polygroup Layer does not exist"));
			return;
		}

		TUniquePtr<FGroupTopology> Topo;
		if (GroupLayer.bDefaultLayer)
		{
			Topo = MakeUnique<FGroupTopology>(&EditMesh, true);
		}
		else
		{
			Topo = MakeUnique<FGroupTopology>(&EditMesh, EditMesh.Attributes()->GetPolygroupLayer(GroupLayer.ExtendedLayerIndex), true);
		}

		FPolygroupRemesh Simplifier(&EditMesh, Topo.Get(), ConstrainedDelaunayTriangulate<double>);
		Simplifier.SimplificationAngleTolerance = Options.AngleThreshold;
		Simplifier.Compute();

		if (Options.bAutoCompact)
		{
			EditMesh.CompactInPlace();
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



namespace UE::PrivateSimplifyHelper
{
	bool UEStandardEditorSimplify(FDynamicMesh3& Mesh, bool bTargetIsTriCount, int32 TargetCount = 0.f)
	{
#if WITH_EDITOR
		IMeshReductionManagerModule* MeshReductionModule = FModuleManager::Get().LoadModulePtr<IMeshReductionManagerModule>("MeshReductionInterface");
		if (!MeshReductionModule)
		{
			UE_LOG(LogGeometry, Warning, TEXT("Failed to load mesh reduction module; cannot simplify mesh"));
			return false;
		}
		IMeshReduction* MeshReduction = MeshReductionModule->GetStaticMeshReductionInterface();
		if (!MeshReduction)
		{
			UE_LOG(LogGeometry, Warning, TEXT("Failed to load mesh reduction interface; cannot simplify mesh"));
			return false;
		}

		FMeshDescription SrcMeshDescription;
		FStaticMeshAttributes Attributes(SrcMeshDescription);
		Attributes.Register();

		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Mesh, SrcMeshDescription, true);
		float Percent = (float)TargetCount / (float)(bTargetIsTriCount ? Mesh.TriangleCount() : Mesh.VertexCount());
		return FSimplifyMeshOp::ComputeStandardSimplifier(MeshReduction, SrcMeshDescription, Mesh, Percent, bTargetIsTriCount, false, nullptr);
#else
		return false;
#endif
	}


	enum class ESimplifyTargetMode
	{
		TriangleCount,
		VertexCount,
		EdgeLength
	};

	template<typename SimplificationType>
	static void DoSimplifyMesh(
		FDynamicMesh3& EditMesh,
		FGeometryScriptSimplifyMeshOptions Options,
		ESimplifyTargetMode SimplifyTargetMode,
		int32 TargetCount,
		double TargetMinEdgeLength = 0.,
		FMeshProjectionTarget* ProjectionTarget = nullptr,
		double GeometricTolerance = -1 // Note: Values < 0 will skip using GeometricTolerance
	)
	{
		SimplificationType Simplifier(&EditMesh);

		Simplifier.ProjectionMode = SimplificationType::ETargetProjectionMode::NoProjection;
		if (ProjectionTarget != nullptr)
		{
			Simplifier.SetProjectionTarget(ProjectionTarget);
		}

		Simplifier.DEBUG_CHECK_LEVEL = 0;
		Simplifier.bRetainQuadricMemory = Options.bRetainQuadricMemory;
		Simplifier.bAllowSeamCollapse = Options.bAllowSeamCollapse;
		if (Options.bAllowSeamCollapse)
		{
			Simplifier.SetEdgeFlipTolerance(1.e-5);
			if (EditMesh.HasAttributes())
			{
				EditMesh.Attributes()->SplitAllBowties();	// eliminate any bowties that might have formed on attribute seams.
			}
		}

		// do these flags matter here since we are not flipping??
		EEdgeRefineFlags MeshBoundaryConstraints = EEdgeRefineFlags::NoFlip;
		EEdgeRefineFlags GroupBorderConstraints = EEdgeRefineFlags::NoConstraint;
		EEdgeRefineFlags MaterialBorderConstraints = EEdgeRefineFlags::NoConstraint;

		FMeshConstraints Constraints;
		FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, EditMesh,
			MeshBoundaryConstraints, GroupBorderConstraints, MaterialBorderConstraints,
			Options.bAllowSeamSplits, Options.bAllowSeamSmoothing, Options.bAllowSeamCollapse);
		Simplifier.SetExternalConstraints(MoveTemp(Constraints));

		if (Options.bPreserveVertexPositions)
		{
			Simplifier.CollapseMode = SimplificationType::ESimplificationCollapseModes::MinimalExistingVertexError;
		}

		if (ProjectionTarget != nullptr && GeometricTolerance >= 0)
		{
			Simplifier.GeometricErrorConstraint = SimplificationType::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
			Simplifier.GeometricErrorTolerance = GeometricTolerance;
		}

		if (SimplifyTargetMode == ESimplifyTargetMode::TriangleCount)
		{
			Simplifier.SimplifyToTriangleCount(FMath::Max(1, TargetCount));
		}
		else if (SimplifyTargetMode == ESimplifyTargetMode::VertexCount)
		{
			Simplifier.SimplifyToVertexCount(FMath::Max(1, TargetCount));
		}
		else // SimplifyTargetMode == ESimplifyTargetMode::EdgeLength
		{
			Simplifier.SimplifyToEdgeLength(TargetMinEdgeLength);
		}

		if (Options.bAutoCompact)
		{
			EditMesh.CompactInPlace();
		}
	}

	static void DoSimplifyMesh(
		EGeometryScriptRemoveMeshSimplificationType SimplificationType,
		FDynamicMesh3& EditMesh,
		FGeometryScriptSimplifyMeshOptions Options,
		ESimplifyTargetMode SimplifyTargetMode,
		int32 TargetCount,
		double TargetMinEdgeLength = 0.,
		FMeshProjectionTarget* ProjectionTarget = nullptr,
		double GeometricTolerance = -1 // Note: Values < 0 will skip using GeometricTolerance
	)
	{
		if (Options.Method == EGeometryScriptRemoveMeshSimplificationType::AttributeAware)
		{
			DoSimplifyMesh<FAttrMeshSimplification>(EditMesh, Options, SimplifyTargetMode, TargetCount, TargetMinEdgeLength, ProjectionTarget, GeometricTolerance);
		}
		else if (Options.Method == EGeometryScriptRemoveMeshSimplificationType::VolumePreserving)
		{
			DoSimplifyMesh<FVolPresMeshSimplification>(EditMesh, Options, SimplifyTargetMode, TargetCount, TargetMinEdgeLength, ProjectionTarget, GeometricTolerance);
		}
		else // EGeometryScriptRemoveMeshSimplificationType::StandardQEM
		{
			DoSimplifyMesh<FQEMSimplification>(EditMesh, Options, SimplifyTargetMode, TargetCount, TargetMinEdgeLength, ProjectionTarget, GeometricTolerance);
		}
	}
}

UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyEditorSimplifyToTriangleCount(
	UDynamicMesh* TargetMesh,
	int32 TriangleCount,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyEditorSimplifyToTriangleCount_InvalidInput", "ApplyEditorSimplifyToTriangleCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		UE::PrivateSimplifyHelper::UEStandardEditorSimplify(EditMesh, true, TriangleCount);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyEditorSimplifyToVertexCount(
	UDynamicMesh* TargetMesh,
	int32 VertexCount,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyEditorSimplifyToVertexCount_InvalidInput", "ApplySimplifyToVertexCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		UE::PrivateSimplifyHelper::UEStandardEditorSimplify(EditMesh, false, VertexCount);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(
	UDynamicMesh* TargetMesh,
	int32 TriangleCount,
	FGeometryScriptSimplifyMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToTriangleCount_InvalidInput", "ApplySimplifyToTriangleCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		using namespace UE::PrivateSimplifyHelper;
		DoSimplifyMesh(Options.Method, EditMesh, Options, ESimplifyTargetMode::TriangleCount, TriangleCount);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToVertexCount(
	UDynamicMesh* TargetMesh,
	int32 VertexCount,
	FGeometryScriptSimplifyMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToVertexCount_InvalidInput", "ApplySimplifyToVertexCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		using namespace UE::PrivateSimplifyHelper;
		DoSimplifyMesh(Options.Method, EditMesh, Options, ESimplifyTargetMode::VertexCount, VertexCount);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTolerance(
	UDynamicMesh* TargetMesh,
	float Tolerance,
	FGeometryScriptSimplifyMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToTolerance_InvalidInput", "ApplySimplifyToTolerance: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		FDynamicMesh3 TempCopy;
		TempCopy.Copy(EditMesh, false, false, false, false);
		FDynamicMeshAABBTree3 Spatial(&TempCopy, true);
		FMeshProjectionTarget ProjTarget(&TempCopy, &Spatial);
		float UseTolerance = FMath::Max(0.0, Tolerance);

		using namespace UE::PrivateSimplifyHelper;
		DoSimplifyMesh(Options.Method, EditMesh, Options, ESimplifyTargetMode::TriangleCount, 1, -1., &ProjTarget, UseTolerance);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToEdgeLength(
	UDynamicMesh* TargetMesh,
	double EdgeLength,
	FGeometryScriptSimplifyMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToVertexCount_InvalidInput", "ApplySimplifyToVertexCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&EdgeLength, &Options](FDynamicMesh3& EditMesh) 
	{
		// Clamp target length to non-negative
		double UseEdgeLength = FMath::Max(0.f, EdgeLength);

		using namespace UE::PrivateSimplifyHelper;
		DoSimplifyMesh(Options.Method, EditMesh, Options, ESimplifyTargetMode::EdgeLength, 1, UseEdgeLength);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyClusterSimplifyToEdgeLength(
	UDynamicMesh* TargetMesh,
	double EdgeLength,
	FGeometryScriptClusterSimplifyMeshOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplySimplifyToVertexCount_InvalidInput", "ApplySimplifyToVertexCount: TargetMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&EdgeLength, &Options](FDynamicMesh3& EditMesh) 
	{
		// Clamp target length to non-negative
		double UseEdgeLength = FMath::Max(0.f, EdgeLength);

		MeshClusterSimplify::FSimplifyOptions SimplifyOptions;
		SimplifyOptions.TargetEdgeLength = UseEdgeLength;
		SimplifyOptions.FixBoundaryAngleTolerance = Options.FixBoundaryAngleTolerance;

		auto ConvertConstraintLevel = [](EGeometryScriptClusterSimplifyConstraintLevel ConstraintLevel) -> MeshClusterSimplify::FSimplifyOptions::EConstraintLevel
			{
				switch (ConstraintLevel)
				{
				default:
					ensure(false);
				case EGeometryScriptClusterSimplifyConstraintLevel::Free:
					return MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Free;
				case EGeometryScriptClusterSimplifyConstraintLevel::Constrained:
					return MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Constrained;
				case EGeometryScriptClusterSimplifyConstraintLevel::Fixed:
					return MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Fixed;
				}
			};
		SimplifyOptions.bTransferGroups = true;

		SimplifyOptions.PreserveEdges.Boundary = ConvertConstraintLevel(Options.EdgeConstraints.MeshBoundary);
		SimplifyOptions.PreserveEdges.PolyGroup = ConvertConstraintLevel(Options.EdgeConstraints.GroupBoundary);
		SimplifyOptions.PreserveEdges.Material = ConvertConstraintLevel(Options.EdgeConstraints.MaterialBoundary);
		SimplifyOptions.PreserveEdges.UVSeam = ConvertConstraintLevel(Options.EdgeConstraints.UVSeam);
		SimplifyOptions.PreserveEdges.ColorSeam = ConvertConstraintLevel(Options.EdgeConstraints.ColorSeam);
		SimplifyOptions.PreserveEdges.NormalSeam = ConvertConstraintLevel(Options.EdgeConstraints.NormalSeam);

		// Note: Currently not exposing tangent seam constraints; they usually should be left free
		SimplifyOptions.PreserveEdges.TangentSeam = MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Free;

		if (Options.bDiscardAttributes)
		{
			// if discarding attributes, also discard constraints from the attribute layer
			SimplifyOptions.PreserveEdges.SetSeamConstraints(MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Free);
			SimplifyOptions.PreserveEdges.Material = MeshClusterSimplify::FSimplifyOptions::EConstraintLevel::Free;
			SimplifyOptions.bTransferAttributes = false;
		}
		
		FDynamicMesh3 ResultMesh;
		MeshClusterSimplify::Simplify(EditMesh, ResultMesh, SimplifyOptions);
		EditMesh = ResultMesh;
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE
