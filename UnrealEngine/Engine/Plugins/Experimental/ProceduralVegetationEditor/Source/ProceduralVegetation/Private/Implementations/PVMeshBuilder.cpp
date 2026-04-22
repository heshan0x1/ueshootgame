// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVMeshBuilder.h"

#include <Facades/PVProfileFacade.h>

#include "ProceduralVegetationModule.h"
#include "Algo/MaxElement.h"
#include "Facades/PVBoneFacade.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVBudVectorsFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPlantFacade.h"
#include "Facades/PVPointFacade.h"
#include "Facades/PVTreeFacade.h"

void MergeMaterials(const TArray<FTrunkGenerationMaterialSetup>& InMaterialSetups, TArray<FLocalDynamicMeshData>& MeshDatas)
{
	TMap<FString, int32> MaterialPathIDMap;
	for (auto& MeshData : MeshDatas)
	{
		TObjectPtr<UMaterialInterface> Material;
		if (InMaterialSetups.IsValidIndex(MeshData.MaterialID))
		{
			Material = InMaterialSetups[MeshData.MaterialID].Material;
		}

		FString MaterialPathName = "";
		if (Material)
		{
			MaterialPathName = Material->GetPathName();
		}

		if (!MaterialPathIDMap.Contains(MaterialPathName))
		{
			MaterialPathIDMap.Add(MaterialPathName , MeshData.MaterialID);
		}
		else
		{
			MeshData.MaterialID = MaterialPathIDMap[MaterialPathName];
		}
	}
}

void FPVMeshBuilder::GenerateGeometryCollection(const FManagedArrayCollection& InSkeletonCollection, const FPVMeshBuilderParams& MeshBuilderParams,
                                                FGeometryCollection& OutGeometryCollection)
{
	InSkeletonCollection.CopyTo(&OutGeometryCollection);

	TArray<FLocalDynamicMeshData> MeshesDataCollection;

	PV::Facades::FBoneFacade::DefineSchema(OutGeometryCollection);
	PV::Facades::FBoneFacade BoneFacade = PV::Facades::FBoneFacade(OutGeometryCollection);

	PV::Facades::FBranchFacade BranchFacade = PV::Facades::FBranchFacade(OutGeometryCollection);

	if (BranchFacade.GetElementCount() == 0)
	{
		UE_LOG(LogProceduralVegetation, Log, TEXT("No branch data available for generating mesh."));
		return;
	}

	SetNjordPixelID(OutGeometryCollection);

	MeshBuilderParams.MaterialSettings.ApplyMaterialSettings(OutGeometryCollection);
	GetLocalDynamicMeshData(OutGeometryCollection, MeshBuilderParams, MeshesDataCollection);

	uint32 VertexCount = 0;
	uint32 TriangleCount = 0;
	for (auto [Vertices, Triangles, MaterialID] : MeshesDataCollection)
	{
		VertexCount += Vertices.Num();
		TriangleCount += Triangles.Num();
	}

	TManagedArray<int32>& PointIDs = BoneFacade.ModifyVertexPointIds();

	OutGeometryCollection.AddElements(1, OutGeometryCollection.TransformGroup);
	OutGeometryCollection.AddElements(1, OutGeometryCollection.GeometryGroup);
	OutGeometryCollection.AddElements(TriangleCount, OutGeometryCollection.FacesGroup);
	OutGeometryCollection.AddElements(VertexCount, OutGeometryCollection.VerticesGroup);
	OutGeometryCollection.SetNumUVLayers(3);

	TManagedArray<FVector3f>& Vertices = OutGeometryCollection.Vertex;
	TManagedArray<FVector3f>& Normals = OutGeometryCollection.Normal;
	TManagedArray<FVector2f>& UVs = *OutGeometryCollection.FindUVLayer(0);
	TManagedArray<FVector2f>& UVs1 = *OutGeometryCollection.FindUVLayer(1);
	TManagedArray<FVector2f>& UVs2 = *OutGeometryCollection.FindUVLayer(2);
	TManagedArray<FIntVector>& Indices = OutGeometryCollection.Indices;
	TManagedArray<int32>& MaterialIDAttribute = OutGeometryCollection.MaterialID;

	const FName MaterialPathAttributeName("MaterialPath");
	auto& MaterialArray = OutGeometryCollection.AddAttribute<FString>(MaterialPathAttributeName, FGeometryCollection::MaterialGroup);
	auto& MaterialSetups = MeshBuilderParams.MaterialSettings.MaterialSetups;

	struct FSection
	{
		int32 MaterialID;
		int32 FirstIndex;
		int32 NumTriangles;
		int32 MinVertexIndex;
		int32 MaxVertexIndex;
	};

	int32 VertexIndex = 0;
	int32 FaceIndex = 0;
	int32 PrevMaterialID = -1;
	TArray<FSection> Sections;

	//Merge MaterialIds based on material Paths
	MergeMaterials(MaterialSetups, MeshesDataCollection);

	//Sorting by material id so sections are created according to material ids
	MeshesDataCollection.Sort([](const FLocalDynamicMeshData& A, const FLocalDynamicMeshData& B)
		{
			return A.MaterialID > B.MaterialID;
		});

	int32 SectionId = -1;
	for (auto MeshIndex = MeshesDataCollection.Num() - 1; MeshIndex >= 0; --MeshIndex)
	{
		int MaterialID = MeshesDataCollection[MeshIndex].MaterialID;
		if (PrevMaterialID != MaterialID)
		{
			PrevMaterialID = MaterialID;

			TObjectPtr<UMaterialInterface> SectionMaterial;

			if (MaterialSetups.IsValidIndex(MaterialID))
			{
				SectionMaterial = MaterialSetups[MaterialID].Material;
			}
			else
			{
				UE_LOG(LogProceduralVegetation, Warning, TEXT("Unassigned Material for section %i, MaterialID %i not found in MaterialSetups."),
					SectionId, MaterialID);
			}

			FString MaterialPathName = "";
			if (SectionMaterial)
			{
				MaterialPathName = SectionMaterial->GetPathName();
			}
			else
			{
				UE_LOG(LogProceduralVegetation, Warning, TEXT("Null Material for section %i, Assign Material slot no %i in MaterialSetups."),
					SectionId, MaterialID);
			}

			int Element = OutGeometryCollection.AddElements(1, FGeometryCollection::MaterialGroup);
			MaterialArray[Element] = SectionMaterial.GetPathName();
			Sections.Add({MaterialID, FaceIndex, 0, VertexIndex, 0});
			SectionId++;
		}

		const FLocalDynamicMeshData& MeshesData = MeshesDataCollection[MeshIndex];
		for (const FIntVector4& Triangle : MeshesData.Triangles)
		{
			const FIntVector4 OffsetTriangle = Triangle + FIntVector4(VertexIndex);
			Indices[FaceIndex] = FIntVector(OffsetTriangle);
			MaterialIDAttribute[FaceIndex] = SectionId;
			++FaceIndex;
		}
		for (const FLocalDynamicMeshData::FVertex& Vertex : MeshesData.Vertices)
		{
			Vertices[VertexIndex] = static_cast<FVector3f>(Vertex.Position);
			Normals[VertexIndex] = static_cast<FVector3f>(Vertex.Normal);
			UVs[VertexIndex] = Vertex.UV;
			UVs1[VertexIndex] = Vertex.UV1;
			UVs2[VertexIndex] = Vertex.UV2;
			PointIDs[VertexIndex] = Vertex.NjordPixelId;
			++VertexIndex;
		}

		int32 CurrentSectionIndex = Sections.Num() - 1;
		if (Sections.IsValidIndex(CurrentSectionIndex))
		{
			Sections[CurrentSectionIndex].MaxVertexIndex = VertexIndex;
			Sections[CurrentSectionIndex].NumTriangles = FaceIndex - Sections[CurrentSectionIndex].FirstIndex;
		}
	}

	OutGeometryCollection.SimulationType[0] = FGeometryCollection::ESimulationTypes::FST_Rigid;
	OutGeometryCollection.TransformToGeometryIndex[0] = 0;
	OutGeometryCollection.TransformIndex[0] = 0;
	OutGeometryCollection.BoneName[0] = "TreeTest";
	OutGeometryCollection.BoneColor[0] = FLinearColor::White;
	OutGeometryCollection.FaceStart[0] = 0;
	OutGeometryCollection.FaceCount[0] = TriangleCount;
	OutGeometryCollection.VertexStart[0] = 0;
	OutGeometryCollection.VertexCount[0] = VertexCount;

	OutGeometryCollection.Visible.Fill(true);
	OutGeometryCollection.BoneMap.Fill(0);
	OutGeometryCollection.TangentU.Fill(FVector3f::ForwardVector);
	OutGeometryCollection.TangentV.Fill(FVector3f::RightVector);
	OutGeometryCollection.Color.Fill(FLinearColor::White);

	int32 SectionIndex = 0;
	for (auto Section : Sections)
	{
		TManagedArray<FGeometryCollectionSection>& GeometrySections = OutGeometryCollection.Sections;

		GeometrySections[SectionIndex].MaterialID = Section.MaterialID;
		GeometrySections[SectionIndex].FirstIndex = Section.FirstIndex;
		GeometrySections[SectionIndex].NumTriangles = Section.NumTriangles;
		GeometrySections[SectionIndex].MinVertexIndex = Section.MinVertexIndex;
		GeometrySections[SectionIndex].MaxVertexIndex = Section.MaxVertexIndex;

		SectionIndex++;
	}

	OutGeometryCollection.UpdateBoundingBox();
}

void FPVMeshBuilder::SetNjordPixelID(FGeometryCollection& OutGeometryCollection)
{
	PV::Facades::FPointFacade PointFacade(OutGeometryCollection);
	TManagedArray<float>& NjordPixelIds = PointFacade.ModifyNjordPixelIDs();

	TMap<int, TArray<int>> BudNumberPointIndicesMap;
	for (int PointIndex = 0; PointIndex < PointFacade.GetElementCount(); PointIndex++)
	{
		int BudNumber = PointFacade.GetBudNumber(PointIndex);
		BudNumberPointIndicesMap.FindOrAdd(BudNumber).Add(PointIndex);
	}

	for (const auto& KeyValuePair : BudNumberPointIndicesMap)
	{
		int ValueIndex = 0;
		for (const auto Value : KeyValuePair.Value)
		{
			//Modify NjordPixelID
			float NjordPixelID = KeyValuePair.Key + static_cast<float>(ValueIndex) / KeyValuePair.Value.Num();
			NjordPixelIds[Value] = NjordPixelID;
			ValueIndex++;
		}
	}
}

void FPVMeshBuilder::GenerateDynamicMesh(FManagedArrayCollection& Collection, const FPVMeshBuilderParams& MeshBuilderParams,
                                         TObjectPtr<UDynamicMesh>& OutMesh)
{
	TArray<FLocalDynamicMeshData> MeshesDataCollection;
	GetLocalDynamicMeshData(Collection, MeshBuilderParams, MeshesDataCollection);

	uint32 VertexCount = 0;
	uint32 TriangleCount = 0;
	for (auto [Vertices, Triangles , MaterialID] : MeshesDataCollection)
	{
		VertexCount += Vertices.Num();
		TriangleCount += Triangles.Num();
	}

	OutMesh->InitializeMesh();
	OutMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& InternalMesh)
			{
				FPVMeshGenerator Generator(VertexCount, TriangleCount, &MeshesDataCollection);
				Generator.Generate();
				InternalMesh.Copy(&Generator);
			},
		EDynamicMeshChangeType::MeshChange);
}

TSet<int32> FPVMeshBuilder::CollectHardPoints(const PV::Facades::FBranchFacade& BranchFacade, const PV::Facades::FPointFacade& PointFacade,
                                              const PV::Facades::FPlantFacade& PlantFacade)
{
	TSet<int32> HardPoints;
	TSet<int32> BranchSourceBudNumbers;
	BranchSourceBudNumbers.Reserve(BranchFacade.GetElementCount());
	TSet<int32> BranchSourcePointIndices;
	BranchSourcePointIndices.Reserve(BranchFacade.GetElementCount());

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const int32 SourceBudNumber = BranchFacade.GetBranchSourceBudNumber(BranchIndex);
		BranchSourceBudNumbers.Add(SourceBudNumber);

		const int32 ParentBranchIndex = BranchFacade.GetParentBranchIndex(BranchIndex);

		if (const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);
			ParentBranchIndex != INDEX_NONE
			&& BranchPoints.Num() > 0)
		{
			const FVector3f FirstPointPosition = PointFacade.GetPosition(BranchPoints[0]);
			const TArray<int32>& ParentBranchPoints = BranchFacade.GetPoints(ParentBranchIndex);
			const int32* Closest = Algo::MinElement(ParentBranchPoints,
				[FirstPointPosition, PointFacade](const int32& PointIndexA, const int32& PointIndexB)
					{
						return FVector3f::Dist(PointFacade.GetPosition(PointIndexA), FirstPointPosition) <
							FVector3f::Dist(PointFacade.GetPosition(PointIndexB), FirstPointPosition);
					});
			const int32 ClosestPointIndex = Closest
				? *Closest
				: INDEX_NONE;

			if (ClosestPointIndex != INDEX_NONE)
			{
				BranchSourcePointIndices.Add(ClosestPointIndex);
			}
		}
	}

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);
		if (BranchPoints.Num() == 0)
		{
			continue;
		}

		const int32 LastPointIndex = BranchPoints.Last();
		const int32 FirstPointIndex = BranchPoints[0];

		for (int32 i = 0; i < BranchPoints.Num(); ++i)
		{
			const int32 PointIndex = BranchPoints[i];
			const int32 PointBudNumber = PointFacade.GetBudNumber(PointIndex);
			const float NjordPixelIndex = PointFacade.GetNjordPixelIndex(PointIndex);

			bool bIsSeedPoint = PlantFacade.IsTrunkIndex(BranchIndex) && PointIndex == FirstPointIndex;
			bool bIsBranchJoint = ((NjordPixelIndex == FMath::FloorToFloat(NjordPixelIndex))
					&& BranchSourceBudNumbers.Contains(PointBudNumber))
				|| BranchSourcePointIndices.Contains(PointIndex);

			if (PointIndex == FirstPointIndex ||
				PointIndex == LastPointIndex ||
				bIsSeedPoint ||
				bIsBranchJoint
			)
			{
				HardPoints.Add(PointIndex);
			}
		}
	}

	return HardPoints;
}

TSet<int32> FPVMeshBuilder::ComputePointGradients(const PV::Facades::FPointFacade& PointFacade,
                                                  const FPVMeshBuilderParams& MeshBuilderParams, const TSet<int32>& HardPoints,
                                                  const float MaxPointScale, TArray<float>& OutMeshDivisionsGradients,
                                                  TArray<float>& OutDeltaModifiers)
{
	TSet<int32> PointsToRemove;
	OutMeshDivisionsGradients.Reserve(PointFacade.GetElementCount());
	OutDeltaModifiers.Reserve(PointFacade.GetElementCount());

	for (int32 PointIndex = 0; PointIndex < PointFacade.GetElementCount(); ++PointIndex)
	{
		const float HullGradient = PointFacade.GetHullGradient(PointIndex);
		const float MainTrunkGradient = PointFacade.GetMainTrunkGradient(PointIndex);
		const float GroundGradient = PointFacade.GetGroundGradient(PointIndex);
		const float ScaleGradient = PointFacade.GetPointScaleGradient(PointIndex);

		// Compute scale removal gradient
		const float PointHullGradient =
			MeshBuilderParams.HullRetentionGradient.GetRichCurveConst()->Eval(1.0f - HullGradient) * MeshBuilderParams.HullRetention;

		const float PointMainTrunkGradient =
			MeshBuilderParams.MainTrunkRetentionGradient.GetRichCurveConst()->Eval(1.0f - MainTrunkGradient) * MeshBuilderParams.MainTrunkRetention;

		const float PointGroundGradient =
			MeshBuilderParams.GroundRetentionGradient.GetRichCurveConst()->Eval(1.0f - GroundGradient) * MeshBuilderParams.GroundRetention;

		const float ScaleRemovalGradient =
			(ScaleGradient + PointHullGradient + PointMainTrunkGradient + PointGroundGradient) * MaxPointScale / 100.0f;

		// Compute mesh divisions gradient
		const float DivisionsScaleGradient = MeshBuilderParams.ScaleRetentionGradient.GetRichCurveConst()->Eval(1.0f - ScaleGradient);
		const float DivisionsHullGradient = MeshBuilderParams.HullRetentionGradient.GetRichCurveConst()->Eval(1.0f - HullGradient);
		const float DivisionsMainTrunkGradient = MeshBuilderParams.MainTrunkRetentionGradient.GetRichCurveConst()->Eval(1.0f - MainTrunkGradient);
		const float DivisionsGroundGradient = MeshBuilderParams.GroundRetentionGradient.GetRichCurveConst()->Eval(1.0f - GroundGradient);

		const float InterpolatedHullGradient = FMath::GetMappedRangeValueClamped(
			FVector2f(0.0f, 1.0f),
			FVector2f(DivisionsHullGradient, 1.0f),
			1.0f - MeshBuilderParams.HullRetention);
		const float InterpolatedMainTrunkGradient = FMath::GetMappedRangeValueClamped(
			FVector2f(0.0f, 1.0f),
			FVector2f(DivisionsMainTrunkGradient, 1.0f),
			1.0f - MeshBuilderParams.MainTrunkRetention);
		const float InterpolatedGroundGradient = FMath::GetMappedRangeValueClamped(
			FVector2f(0.0f, 1.0f),
			FVector2f(DivisionsGroundGradient, 1.0f),
			1.0f - MeshBuilderParams.GroundRetention);

		const float MeshDivisionsGradient = DivisionsScaleGradient * InterpolatedHullGradient * InterpolatedMainTrunkGradient *
			InterpolatedGroundGradient;
		OutMeshDivisionsGradients.Add(MeshDivisionsGradient);

		// Compute delta modifier gradient
		const float SegmentScaleGradient = MeshBuilderParams.ScaleRetentionGradient.GetRichCurveConst()->Eval(1.0f - ScaleGradient);
		const float SegmentHullGradient = MeshBuilderParams.HullRetentionGradient.GetRichCurveConst()->Eval(1.0f - HullGradient);
		const float SegmentMainTrunkGradient = MeshBuilderParams.MainTrunkRetentionGradient.GetRichCurveConst()->Eval(1.0f - MainTrunkGradient);
		const float SegmentGroundGradient = MeshBuilderParams.GroundRetentionGradient.GetRichCurveConst()->Eval(1.0f - GroundGradient);

		const float DeltaModifierGradient =
			(SegmentScaleGradient * MeshBuilderParams.ScaleRetention) +
			(SegmentHullGradient * MeshBuilderParams.HullRetention) +
			(SegmentMainTrunkGradient * MeshBuilderParams.MainTrunkRetention) +
			(SegmentGroundGradient * MeshBuilderParams.GroundRetention);

		// Should the point be removed per scale gradient
		if (!HardPoints.Contains(PointIndex) && MeshBuilderParams.PointRemoval > 0.0f && ScaleRemovalGradient < MeshBuilderParams.PointRemoval)
		{
			PointsToRemove.Add(PointIndex);
		}

		// Compute delta modifier
		float DeltaModifier = 1.0f;
		if (!HardPoints.Contains(PointIndex))
		{
			DeltaModifier = MeshBuilderParams.SegmentRetentionImpact * DeltaModifierGradient;
		}
		OutDeltaModifiers.Add(DeltaModifier);
	}

	return PointsToRemove;
}

float FPVMeshBuilder::GetMaxDeltaBetweenHardPoints(const PV::Facades::FBranchFacade& BranchFacade, const PV::Facades::FPointFacade& PointFacade,
                                                   const TSet<int32>& HardPoints)
{
	float MaxDeltaBetweenHardPoints = 0.0f;

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);

		if (BranchPoints.Num() < 2)
		{
			continue;
		}

		TArray<int32> IndicesOfHardPointsForBranch;
		IndicesOfHardPointsForBranch.Add(0);

		for (int32 Idx = 1; Idx < BranchPoints.Num() - 1; ++Idx)
		{
			if (HardPoints.Contains(BranchPoints[Idx]))
			{
				IndicesOfHardPointsForBranch.Add(Idx);
			}
		}

		IndicesOfHardPointsForBranch.Add(BranchPoints.Num() - 1);

		for (int32 Idx = 0; Idx < IndicesOfHardPointsForBranch.Num() - 1; ++Idx)
		{
			const int32 Pt0Index = BranchPoints[IndicesOfHardPointsForBranch[Idx]];
			const int32 Pt2Index = BranchPoints[IndicesOfHardPointsForBranch[Idx + 1]];
			const FVector Pt0Position = FVector(PointFacade.GetPosition(Pt0Index));
			const FVector Pt2Position = FVector(PointFacade.GetPosition(Pt2Index));

			for (int32 k = IndicesOfHardPointsForBranch[Idx]; k < IndicesOfHardPointsForBranch[Idx + 1]; ++k)
			{
				const int32 Pt1Index = BranchPoints[k];
				const FVector Pt1Position = FVector(PointFacade.GetPosition(Pt1Index));

				if (const float Delta = FMath::PointDistToLine(Pt1Position, Pt2Position - Pt0Position, Pt0Position);
					Delta > MaxDeltaBetweenHardPoints)
				{
					MaxDeltaBetweenHardPoints = Delta;
				}
			}
		}
	}

	return MaxDeltaBetweenHardPoints;
}

void FPVMeshBuilder::PerformPathSimplification(const PV::Facades::FBranchFacade& BranchFacade,
                                               const PV::Facades::FPointFacade& PointFacade, const FPVMeshBuilderParams& MeshBuilderParams,
                                               const float MaxPointScale, const TSet<int32>& HardPoints, const TArray<float>& DeltaModifiers,
                                               TSet<int32>& InOutPointsToRemove)
{
	const int32 Iterations = MeshBuilderParams.Accuracy;
	// Epsilon is the maximum allowed curvature error
	const float Epsilon = FMath::Pow(MeshBuilderParams.SegmentReduction, 2) / static_cast<float>(Iterations);
	const float MaxDeltaBetweenHardPoints = GetMaxDeltaBetweenHardPoints(BranchFacade, PointFacade, HardPoints);

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		TArray<int32> BranchPoints = BranchFacade.GetPoints(BranchIndex).FilterByPredicate(
			[&InOutPointsToRemove](const int32 PointIndex)
				{
					return !InOutPointsToRemove.Contains(PointIndex);
				});

		for (int32 j = 0; j < Iterations; ++j)
		{
			const float IterationEpsilon = Epsilon * (j + 1);
			for (int32 i = 1; i < BranchPoints.Num() - 1; ++i)
			{
				const int32 Pt0Index = BranchPoints[i - 1],
				            Pt1Index = BranchPoints[i],
				            Pt2Index = BranchPoints[i + 1];
				const FVector Pt0Position = FVector(PointFacade.GetPosition(Pt0Index)),
				              Pt1Position = FVector(PointFacade.GetPosition(Pt1Index)),
				              Pt2Position = FVector(PointFacade.GetPosition(Pt2Index));
				const float DeltaModifier = DeltaModifiers[Pt1Index];
				const float Delta = FMath::PointDistToLine(Pt1Position, Pt2Position - Pt0Position, Pt0Position);

				const float NormalizedDelta = FMath::GetMappedRangeValueClamped(
					FVector2f(0.0f, MaxDeltaBetweenHardPoints),
					FVector2f(0.0f, 1.0f),
					Delta);

				if (NormalizedDelta + DeltaModifier < IterationEpsilon && !HardPoints.Contains(Pt1Index))
				{
					InOutPointsToRemove.Add(Pt1Index);
					BranchPoints.Remove(Pt1Index);
					i--;
				}
			}
		}

		// Ensure long chains of removed points do not exceed a maximum threshold
		BranchPoints = BranchFacade.GetPoints(BranchIndex);
		if (BranchPoints.Num() == 0)
		{
			return;
		}

		FVector3f PreviousPointPosition = PointFacade.GetPosition(BranchPoints[0]);
		float TravelDistance = 0.0f;

		for (int32 i = 0; i < BranchPoints.Num(); ++i)
		{
			const int32 CurrentPointIndex = BranchPoints[i];
			FVector3f CurrentPointPosition = PointFacade.GetPosition(CurrentPointIndex);
			const float Distance = FVector3f::Distance(PreviousPointPosition, CurrentPointPosition) / 100.0f;

			if (InOutPointsToRemove.Contains(CurrentPointIndex))
			{
				TravelDistance += Distance;
			}
			else
			{
				TravelDistance = 0.0f;
			}

			if (TravelDistance > MeshBuilderParams.LongestSegmentLength)
			{
				InOutPointsToRemove.Remove(CurrentPointIndex);
				TravelDistance = 0.0f;
			}

			PreviousPointPosition = CurrentPointPosition;
		}

		// Ensure two kept points are never closer than a minimum threshold
		BranchPoints = BranchFacade.GetPoints(BranchIndex).FilterByPredicate(
			[&InOutPointsToRemove](const int32 PointIndex)
				{
					return !InOutPointsToRemove.Contains(PointIndex);
				});
		PreviousPointPosition = PointFacade.GetPosition(BranchPoints[0]);

		for (int32 i = 0; i < BranchPoints.Num(); ++i)
		{
			const int32 CurrentPointIndex = BranchPoints[i];
			FVector3f CurrentPointPosition = PointFacade.GetPosition(CurrentPointIndex);
			const float Distance = FVector3f::Distance(PreviousPointPosition, CurrentPointPosition) / 100.0f;

			if (Distance < MeshBuilderParams.ShortestSegmentLength && !HardPoints.Contains(CurrentPointIndex))
			{
				InOutPointsToRemove.Add(CurrentPointIndex);
			}
			else
			{
				PreviousPointPosition = CurrentPointPosition;
			}
		}
	}
}

TArray<int32> FPVMeshBuilder::ComputeMeshDivisions(const PV::Facades::FBranchFacade& BranchFacade,
                                                   const FPVMeshBuilderParams& MeshBuilderParams, const TArray<float>& MeshDivisionsGradients,
                                                   const int32 PointCount)
{
	TArray<int32> TargetMeshDivisions;
	static constexpr int32 MinimumDivisions = 3;
	TargetMeshDivisions.Init(MinimumDivisions, PointCount);

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);
		const float MinDivisions = static_cast<float>(MeshBuilderParams.MinMeshDivisions);
		const float MaxDivisions = static_cast<float>(MeshBuilderParams.MaxMeshDivisions);
		const FVector2f NormalRange(0.0f, 1.0f);
		const FVector2f DivisionsRange(MinDivisions, MaxDivisions);

		for (int32 i = 0; i < BranchPoints.Num(); ++i)
		{
			const int32 CurrentPointIndex = BranchPoints[i];
			const float MeshDivisionsGradient = MeshDivisionsGradients[CurrentPointIndex];

			const int32 TargetMeshDivisionsForCurrentPoint = FMath::RoundToInt32(
				FMath::GetMappedRangeValueClamped(
					NormalRange,
					DivisionsRange,
					MeshDivisionsGradient));

			TargetMeshDivisions[CurrentPointIndex] = TargetMeshDivisionsForCurrentPoint;
		}
	}

	return TargetMeshDivisions;
}

void FPVMeshBuilder::TriangulateRings(const TArray<int32>& PreviousIndices, const TArray<int32>& CurrentIndices, int32& InOutPolyGroupIndex,
                                      FLocalDynamicMeshData& OutMeshData)
{
	if (!PreviousIndices.IsEmpty())
	[[likely]]
	{
		const float IndexDelta1 = PreviousIndices.Num() < CurrentIndices.Num()
			? static_cast<float>(PreviousIndices.Num()) / CurrentIndices.Num()
			: 1.0f;
		const float IndexDelta2 = CurrentIndices.Num() < PreviousIndices.Num()
			? static_cast<float>(CurrentIndices.Num()) / PreviousIndices.Num()
			: 1.0f;

		for (
			float Index1 = 0.0f, Index2 = 0.0f;
			FMath::RoundToInt32(Index1 + IndexDelta1) <= PreviousIndices.Num() && FMath::RoundToInt32(Index2 + IndexDelta2) <=
			CurrentIndices.Num();
			Index1 += IndexDelta1, Index2 += IndexDelta2
		)
		{
			int32 VertexIndices[4] = {
				PreviousIndices[FMath::RoundToInt32(Index1) % PreviousIndices.Num()],
				CurrentIndices[FMath::RoundToInt32(Index2) % CurrentIndices.Num()],
				PreviousIndices[FMath::RoundToInt32(Index1 + IndexDelta1) % PreviousIndices.Num()],
				CurrentIndices[FMath::RoundToInt32(Index2 + IndexDelta2) % CurrentIndices.Num()],
			};

			if (VertexIndices[0] != VertexIndices[2])
			{
				OutMeshData.Triangles.Emplace(VertexIndices[0], VertexIndices[1], VertexIndices[2], InOutPolyGroupIndex);
			}
			if (VertexIndices[1] != VertexIndices[3])
			{
				OutMeshData.Triangles.Emplace(VertexIndices[1], VertexIndices[3], VertexIndices[2], InOutPolyGroupIndex);
			}

			++InOutPolyGroupIndex;
		}
	}
}

float FPVMeshBuilder::GetProfileMultiplier(const TArray<float>& InProfilePoints, const float ProfileUV_U)
{
	if (InProfilePoints.IsEmpty())
	{
		return 1.0f;
	}

	const int32 MaxPointsIndex = InProfilePoints.Num() - 1;
	const int32 ProfileIndex0 = FMath::Min(FMath::FloorToInt(ProfileUV_U * 100), MaxPointsIndex);
	const int32 ProfileIndex1 = FMath::Min(FMath::CeilToInt(ProfileUV_U * 100), MaxPointsIndex);

	const float Scale0 = InProfilePoints[ProfileIndex0];
	const float Scale1 = InProfilePoints[ProfileIndex1];

	const float BlendValue = FMath::GetMappedRangeValueClamped(
		FVector2f(static_cast<float>(ProfileIndex0), static_cast<float>(ProfileIndex1)),
		FVector2f(0.0f, 1.0f),
		ProfileUV_U * 100);

	const float ProfileMultiplier = FMath::Lerp(Scale0, Scale1, BlendValue);

	return ProfileMultiplier;
}

TMap<int32, TArray<int32>> FPVMeshBuilder::GetPointsIndicesToFoliageIndicesMap(const FManagedArrayCollection& Collection)
{
	TMap<int32, TArray<int32>> Map;
	const PV::Facades::FPointFacade PointFacade(Collection);
	const PV::Facades::FBranchFacade BranchFacade(Collection);
	const PV::Facades::FFoliageFacade FoliageFacade(Collection);

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& FoliageEntryIds = FoliageFacade.GetFoliageEntryIdsForBranch(BranchIndex);
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);

		if (BranchPoints.Num() == 0)
		{
			continue;
		}

		for (int32 FoliageEntryId : FoliageEntryIds)
		{
			// Find the closest point for current entry's pivot position
			const FVector3f PivotPosition = FoliageFacade.GetPivotPoint(FoliageEntryId);
			const int32* Closest = Algo::MinElement(BranchPoints, [PivotPosition, PointFacade](const int32& PointIndexA, const int32& PointIndexB)
				{
					return FVector3f::Dist(PointFacade.GetPosition(PointIndexA), PivotPosition) <
						FVector3f::Dist(PointFacade.GetPosition(PointIndexB), PivotPosition);
				});
			int32 ClosestPointIndex = Closest
				? *Closest
				: BranchPoints[0];

			Map.FindOrAdd(ClosestPointIndex).Add(FoliageEntryId);
		}
	}

	return Map;
}

void FPVMeshBuilder::GenerateBranchMeshData(const bool bPrimitiveIsTrunk, const int32 GenerationNumber, const TArray<int32>& PrimitivePoints,
                                            const FDisplacementData& DisplacementData, const FPVMeshBuilderParams& MeshBuilderParams,
                                            const TArray<int32>& TargetMeshDivisions, const FManagedArrayCollection& Collection,
                                            FLocalDynamicMeshData& OutLocalMeshData)
{
	if (PrimitivePoints.Num() < 2)
	{
		return;
	}
	
	const PV::Facades::FPointFacade PointFacade(Collection);
	const PV::Facades::FBudVectorsFacade BudVectorFacade(Collection);
	const PV::Facades::FPlantProfileFacade PlantProfileFacade(Collection);

	bool bShouldApplyProfile =
		PlantProfileFacade.NumProfileEntries() > 0
		&& MeshBuilderParams.PlantProfileOptions.Num() > 0
		&& (MeshBuilderParams.bApplyProfileToBranches || bPrimitiveIsTrunk);

	TArray<float> ProfilePoints;
	if (bShouldApplyProfile)
	{
		if (const int32 ProfileId = MeshBuilderParams.PlantProfileOptions.Find(MeshBuilderParams.SelectedPlantProfile) - 1;
			ProfileId > -1)
		{
			ProfilePoints = PlantProfileFacade.GetProfilePoints(ProfileId);
		}
		else
		{
			bShouldApplyProfile = false;
		}
	}

	const TManagedArray<FVector3f>& PointPositions = PointFacade.GetPositions();
	const TManagedArray<TArray<FVector3f>>& PointBudDirections = BudVectorFacade.GetBudDirections();
	const TManagedArray<float>& PointScales = PointFacade.GetPointScales();
	
	const float MaxPointScale = *Algo::MaxElement(PointScales);
	const float MaxScaleRatio = 1.0f / (MaxPointScale * UE_TWO_PI);
	int32 PolyGroupIndex = 0;
	TArray<int32> CurrentIndices;
	TArray<int32> PreviousIndices;

	bool bShouldApplyDisplacement =
		DisplacementData.Values.Num() > 0
		&& DisplacementData.TextureWidth > 0
		&& DisplacementData.TextureHeight > 0
		&& GenerationNumber <= MeshBuilderParams.DisplacementGenerationUpperLimit;

	FVector3f PreviousUpVector = FVector3f::ForwardVector;
	FVector3f PreviousAimVector = FVector3f::UpVector;

	float Displacement_UV_V = 0.0f;
	FVector3f PreviousPosition = PointPositions[PrimitivePoints[0]];

	auto SafeDivide = [](const double InNumerator, const double InDenominator)
	{
		return FMath::IsNearlyZero(InDenominator) ? InNumerator : (InNumerator / InDenominator);
	};
	
	for (int32 Index = 0; Index < PrimitivePoints.Num(); ++Index)
	{
		const int32& PointIndex = PrimitivePoints[Index];
		const float NjordPixelId = PointIndex;
		
		const float LengthFromRoot = PointFacade.GetLengthFromRoot(PointIndex);

		float TextureCoordV = PointFacade.GetTextureCoordV(PointIndex);
		float TextureCoordUOffset = PointFacade.GetTextureCoordUOffset(PointIndex);
		FVector2f OldRange = FVector2f(0, 1.0f);
		FVector2f NewRange = PointFacade.GetURange(PointIndex);

		float Interval = NewRange.GetMax() - NewRange.GetMin();
		float URatio = FMath::IsNearlyZero(Interval)
			? 1.0f
			: 1.0f / Interval;
		TextureCoordV /= URatio;

		FVector3f AimVector = PointBudDirections[PointIndex][0].GetUnsafeNormal();
		// The AimVector needs to be adjusted in case curvature has changed during mesh simplification
		const FVector3f& PointPosition = PointPositions[PointIndex];
		FVector3f UpdatedAimVector;
		
		const float PointScale = PointScales[PointIndex];
		Displacement_UV_V += FVector3f::Dist(PointPosition, PreviousPosition) * MaxScaleRatio * (MaxPointScale / PointScale);
		
		if (Index == PrimitivePoints.Num() - 1)
		[[unlikely]]
		{
			const int32 PreviousPointIndex = PrimitivePoints[Index - 1];
			const FVector3f& PreviousPointPosition = PointPositions[PreviousPointIndex];
			UpdatedAimVector = (PointPosition - PreviousPointPosition).GetSafeNormal();
		}
		else
		[[likely]]
		{
			const int32 NextPointIndex = PrimitivePoints[Index + 1];
			const FVector3f& NextPointPosition = PointPositions[NextPointIndex];
			UpdatedAimVector = (NextPointPosition - PointPosition).GetSafeNormal();
		}
		const FQuat4f RotationQuat = FQuat4f::FindBetweenNormals(AimVector, UpdatedAimVector);

		const FVector3f PointUpOriginal = PointBudDirections[PointIndex][5].GetUnsafeNormal();
		const FVector3f PointUpUpdated = RotationQuat.RotateVector(PointUpOriginal);

		FVector3f PointUp = PointUpUpdated;

		FVector3f CrossVector = PointUp.Cross(UpdatedAimVector);
		PointUp = CrossVector.Cross(UpdatedAimVector);

		if (Index > 0)
		{
			if (Index > 0 && FVector3f::DotProduct(PointUp, PreviousUpVector) < 0.0f)
			{
				PointUp = -PointUp;
			}

			const FVector3f InterpolatedAimVector = FMath::Lerp(PreviousAimVector, UpdatedAimVector, 0.5f).GetSafeNormal();
			const FQuat4f AimVectorRotationQuat = FQuat4f::FindBetweenNormals(UpdatedAimVector, InterpolatedAimVector);
			PointUp = AimVectorRotationQuat.RotateVector(PointUp);
			UpdatedAimVector = InterpolatedAimVector;
		}

		PreviousAimVector = UpdatedAimVector;
		PreviousUpVector = PointUp;

		FVector2f UV1 = FVector2f(0, 1.0 - Displacement_UV_V);
		const FVector2f UV2 = FVector2f(LengthFromRoot * 0.01, SafeDivide(PointScale, MaxPointScale));
		const float UVOGenerationOffset = GenerationNumber - 1;

		const float BranchGradient = PointFacade.GetBranchGradient(PointIndex);
		const float ProfileFallOff = MeshBuilderParams.PlantProfileFallOff.GetRichCurveConst()->Eval(BranchGradient);
		if (PointScale == 0.0f)
		[[unlikely]]
		{
			CurrentIndices.Add(OutLocalMeshData.Vertices.Emplace(PointPosition, UpdatedAimVector, FVector2f(UVOGenerationOffset, TextureCoordV), UV1, UV2, NjordPixelId));
		}
		else
		[[likely]]
		{
			const int32 PointDivisions = TargetMeshDivisions[PointIndex];
			const float RotationDelta = UE_TWO_PI / PointDivisions;
			for (int32 Division = 0; Division < PointDivisions; ++Division)
			{
				float UV_U = (1 - (static_cast<float>(Division) / PointDivisions)) + TextureCoordUOffset;
				UV_U = FMath::GetMappedRangeValueUnclamped(OldRange, NewRange, FMath::Wrap(UV_U, OldRange.X, OldRange.Y));
				UV_U += UVOGenerationOffset;

				float Displacement_UV_U = (static_cast<float>(Division) / PointDivisions);
				UV1.X = 1 - Displacement_UV_U;
				
				float ProfileMultiplier = 1.0f;
				if (bShouldApplyProfile)
				{
					ProfileMultiplier = GetProfileMultiplier(ProfilePoints, Displacement_UV_U);
					ProfileMultiplier = FMath::Lerp(1.0f, ProfileMultiplier * MeshBuilderParams.PlantProfileScale, ProfileFallOff);
				}

				const FVector3f Direction = FQuat4f(UpdatedAimVector, RotationDelta * Division).RotateVector(PointUp).GetSafeNormal();
				FVector3f RadialPoint = PointPosition + (Direction * PointScale * ProfileMultiplier);

				if (bShouldApplyDisplacement)
				{
					const int32 X = FMath::Clamp(
						FMath::FloorToInt(
							Displacement_UV_U * MeshBuilderParams.DisplacementUVScale.X * (DisplacementData.TextureWidth - 1)
						) % DisplacementData.TextureWidth,
						0, DisplacementData.TextureWidth - 1
					);
					const int32 Y = FMath::Clamp(
						FMath::FloorToInt(
							Displacement_UV_V * MeshBuilderParams.DisplacementUVScale.Y * (DisplacementData.TextureHeight - 1)
						) % DisplacementData.TextureHeight,
						0, DisplacementData.TextureHeight - 1
					);

					const int32 Idx = Y * DisplacementData.TextureWidth + X;
					float DisplacementMultiplier =
						MeshBuilderParams.DisplacementStrength * (DisplacementData.Values[Idx] - MeshBuilderParams.DisplacementBias);
					DisplacementMultiplier = DisplacementMultiplier * PointScale / MaxPointScale;

					RadialPoint = RadialPoint + (Direction * DisplacementMultiplier);
				}

				const FVector3f Normal = FVector3f((RadialPoint - PointPosition).GetUnsafeNormal());

				CurrentIndices.Add(OutLocalMeshData.Vertices.Emplace(RadialPoint, Normal, FVector2f(UV_U, TextureCoordV), UV1, UV2, NjordPixelId));
			}
		}

		FVector2f UV0 = FVector2f(FMath::GetMappedRangeValueUnclamped(OldRange, NewRange, FMath::Wrap(TextureCoordUOffset, OldRange.X, OldRange.Y)),
				TextureCoordV);
		UV0.X += UVOGenerationOffset;

		UV1.X = 0;
		
		const FLocalDynamicMeshData::FVertex FirstVertex = OutLocalMeshData.Vertices[CurrentIndices[0]];
		CurrentIndices.Add(OutLocalMeshData.Vertices.Emplace(FirstVertex.Position, FirstVertex.Normal,UV0, UV1, UV2 ,NjordPixelId));

		PreviousPosition = PointPosition;
		
		TriangulateRings(PreviousIndices, CurrentIndices, PolyGroupIndex, OutLocalMeshData);

		PreviousIndices = MoveTemp(CurrentIndices);
		CurrentIndices.Empty();
	}
}

void FPVMeshBuilder::UpdateFoliagePivotPoints(const TSet<int32>& PointsToRemove, FManagedArrayCollection& OutCollection)
{
	const PV::Facades::FBranchFacade BranchFacade(OutCollection);
	const PV::Facades::FPointFacade PointFacade(OutCollection);
	PV::Facades::FFoliageFacade FoliageFacade(OutCollection);

	TMap<int32, TArray<int32>> PointsIndicesToFoliageIndicesMap = GetPointsIndicesToFoliageIndicesMap(OutCollection);

	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);

		if (BranchPoints.Num() == 0)
		{
			continue;
		}

		TArray<int32> SortedPointIndices = BranchPoints;
		SortedPointIndices.Sort([PointFacade](const int32& A, const int32& B)
			{
				return PointFacade.GetLengthFromRoot(A) < PointFacade.GetLengthFromRoot(B);
			});

		for (int32 i = 0; i < SortedPointIndices.Num(); ++i)
		{
			const int32 CurrentPointIndex = SortedPointIndices[i];
			if (PointsToRemove.Contains(CurrentPointIndex) &&
				i > 0 &&
				i < SortedPointIndices.Num() - 1 &&
				PointsIndicesToFoliageIndicesMap.Contains(CurrentPointIndex))
			{
				int32 j = i - 1;
				while (j > 0 && PointsToRemove.Contains(SortedPointIndices[j]))
				{
					j--;
				}
				const int32 PreviousPointIndex = SortedPointIndices[j];

				int32 k = i + 1;
				while (k < (SortedPointIndices.Num() - 1) && PointsToRemove.Contains(SortedPointIndices[k]))
				{
					k++;
				}
				const int32 NextPointIndex = SortedPointIndices[k];

				const float PreviousPointLengthFromRoot = PointFacade.GetLengthFromRoot(PreviousPointIndex);
				const float CurrentPointLengthFromRoot = PointFacade.GetLengthFromRoot(CurrentPointIndex);
				const float NextPointLengthFromRoot = PointFacade.GetLengthFromRoot(NextPointIndex);
				const float BlendValue = FMath::GetMappedRangeValueClamped(
					FVector2f(PreviousPointLengthFromRoot, NextPointLengthFromRoot),
					FVector2f(0.0f, 1.0f),
					CurrentPointLengthFromRoot
				);

				const FVector3f& CurrentPointPosition = PointFacade.GetPosition(CurrentPointIndex);
				const FVector3f& PreviousPointPosition = PointFacade.GetPosition(PreviousPointIndex);
				const FVector3f& NextPointPosition = PointFacade.GetPosition(NextPointIndex);

				const FVector3f InterpolatedPointPosition = FMath::Lerp(PreviousPointPosition, NextPointPosition, BlendValue);

				for (const int32 FoliageIndex : PointsIndicesToFoliageIndicesMap[CurrentPointIndex])
				{
					const FVector3f& FoliagePosition = FoliageFacade.GetPivotPoint(FoliageIndex);
					const FVector3f OffsetVector = FoliagePosition - CurrentPointPosition;
					const FVector3f UpdatedFoliagePosition = InterpolatedPointPosition + OffsetVector;

					FoliageFacade.SetPivotPoint(FoliageIndex, UpdatedFoliagePosition);
				}
			}
		}
	}
}

bool IsDisplacementTextureValid(const TObjectPtr<UTexture2D>& Texture)
{
#if WITH_EDITOR
	return Texture && Texture->Source.IsValid();
#else
	return false;
#endif
}

void FPVMeshBuilder::GetLocalDynamicMeshData(FManagedArrayCollection& Collection, const FPVMeshBuilderParams& MeshBuilderParams,
                                             TArray<FLocalDynamicMeshData>& MeshesData)
{
	const PV::Facades::FBranchFacade BranchFacade(Collection);
	const PV::Facades::FPointFacade PointFacade(Collection);
	const PV::Facades::FPlantFacade PlantFacade(Collection);

	if (!BranchFacade.IsValid())
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("Fail to evaluate, InCollection dose not match the schema."));
		return;
	}

	const TManagedArray<float>& PointScales = PointFacade.GetPointScales();
	const float MaxPointScale = *Algo::MaxElement(PointScales);

	const TSet<int32> HardPoints = CollectHardPoints(BranchFacade, PointFacade, PlantFacade);

	TArray<float> MeshDivisionsGradientsForPoints;
	TArray<float> DeltaModifiersForPoints;
	TSet<int32> PointsToRemove = ComputePointGradients(PointFacade, MeshBuilderParams, HardPoints, MaxPointScale, MeshDivisionsGradientsForPoints,
		DeltaModifiersForPoints);

	PerformPathSimplification(BranchFacade, PointFacade, MeshBuilderParams, MaxPointScale, HardPoints, DeltaModifiersForPoints, PointsToRemove);

	// Point removal may update curvate of the branch.
	// Foliage instance pivot/attachment points need to be updated to compensate for that.
	if (PointsToRemove.Num() > 0)
	{
		UpdateFoliagePivotPoints(PointsToRemove, Collection);
	}

	const TArray<int32> TargetMeshDivisions = ComputeMeshDivisions(BranchFacade, MeshBuilderParams, MeshDivisionsGradientsForPoints,
		PointFacade.GetElementCount());

	MeshesData.SetNum(BranchFacade.GetElementCount());

	int32 DisplacementWidth = 0;
	int32 DisplacementHeight = 0;
	if (MeshBuilderParams.DisplacementTexture
		&& IsDisplacementTextureValid(MeshBuilderParams.DisplacementTexture)
		&& MeshBuilderParams.DisplacementValues.Num() > 0
		&& MeshBuilderParams.DisplacementStrength > 0.0f)
	{
#if WITH_EDITOR
		DisplacementWidth = MeshBuilderParams.DisplacementTexture->Source.GetSizeX();
		DisplacementHeight = MeshBuilderParams.DisplacementTexture->Source.GetSizeY();
#endif
		if (MeshBuilderParams.DisplacementTexture->SRGB)
		{
			UE_LOG(LogProceduralVegetation, Warning,
				TEXT("Displacement texture selected has sRGB enabled! This will corrupt displacement data. Please disable it."));
		}
	}

	const FDisplacementData DisplacementData(MeshBuilderParams.DisplacementValues, DisplacementWidth, DisplacementHeight);

	ParallelFor(
		BranchFacade.GetElementCount(),
		[&](const int32 PrimitiveIndex)
			{
				const TArray<int32>& PrimitivePoints = BranchFacade.GetPoints(PrimitiveIndex).FilterByPredicate(
					[&PointsToRemove](const int32 PointIndex)
						{
							return !PointsToRemove.Contains(PointIndex);
						});

				const bool PrimitiveIsTrunk = PlantFacade.IsTrunkIndex(PrimitiveIndex);
				const int32 GenerationNumber = PV::Facades::FTreeFacade::GetGenerationNumberFromSourceBud(Collection, PrimitiveIndex);

				MeshesData[PrimitiveIndex].MaterialID = BranchFacade.GetBranchUVMaterial(PrimitiveIndex);

				GenerateBranchMeshData(PrimitiveIsTrunk, GenerationNumber, PrimitivePoints, DisplacementData, MeshBuilderParams, TargetMeshDivisions,
					Collection, MeshesData[PrimitiveIndex]);
			});
}

bool FPVMeshBuilder::ExtractDisplacementData(const TObjectPtr<UTexture2D>& Texture, TArray<float>& OutValues, FString& OutError)
{
	OutValues.Empty();

#if WITH_EDITOR
	if (!Texture)
	{
		OutError = "ExtractDisplacementData: Texture is null.";
		UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"), *OutError);
		return false;
	}

	FTextureSource& Source = Texture->Source;
	if (!Source.IsValid())
	{
		OutError = FString::Printf(TEXT("ExtractDisplacementData: No source data found on texture %s."), *Texture->GetName());
		UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"), *OutError);
		return false;
	}

	if (!Source.AreAllBlocksPowerOfTwo())
	{
		OutError = FString::Printf(TEXT("ExtractDisplacementData: Texture %s is not power of two, Only Power of 2 textures are supported."), *Texture->GetName());
		UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"), *OutError);
		return false;
	}

	const int32 Width = Source.GetSizeX();
	const int32 Height = Source.GetSizeY();
	const int32 NumPix = Width * Height;
	OutValues.SetNumZeroed(NumPix);

	const uint8* RawData = Source.LockMipReadOnly(0);

	bool bSuccess = true;
	switch (Source.GetFormat())
	{
	case TSF_RGBA32F:
		{
			const float* FloatData = reinterpret_cast<const float*>(RawData);
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = FloatData[i * 4];
			}
		}
		break;
	case TSF_R32F:
		{
			const float* FloatData = reinterpret_cast<const float*>(RawData);
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = FloatData[i];
			}
		}
		break;
	case TSF_RGBA16F:
		{
			const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(RawData);
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = HalfData[i * 4].GetFloat();
			}
		}
		break;
	case TSF_R16F:
		{
			const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(RawData);
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = HalfData[i].GetFloat();
			}
		}
		break;
	case TSF_BGRA8:
		{
			const FColor* ColorData = reinterpret_cast<const FColor*>(RawData);
			constexpr float Inv255 = 1.0f / 255.0f;
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = ColorData[i].R * Inv255;
			}
		}
		break;
		case TSF_G8:
		{
			const uint8* ColorData = reinterpret_cast<const uint8*>(RawData);
			constexpr float Inv255 = 1.0f / 255.0f;
			for (int32 i = 0; i < NumPix; ++i)
			{
				OutValues[i] = *ColorData * Inv255;
				ColorData++;
			}
		}
		break;
	default:
		OutError = FString::Printf(TEXT("ExtractDisplacementData: Unsupported source format %d on texture %s."), Source.GetFormat(), *Texture->GetName());
		UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"),*OutError);
		bSuccess = false;
		break;
	}


	Source.UnlockMip(0);
	return bSuccess;
#else
	OutError = "ExtractDisplacementData only works in editor builds (WITH_EDITOR).";
	UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"), *OutError);
	return false;
#endif
}
