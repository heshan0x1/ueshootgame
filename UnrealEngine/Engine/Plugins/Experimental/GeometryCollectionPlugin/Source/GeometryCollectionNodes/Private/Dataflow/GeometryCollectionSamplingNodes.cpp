// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSamplingNodes.h"

#include "UDynamicMesh.h"
#include "Spatial/FastWinding.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionSamplingNodes)

namespace UE::Dataflow
{

	void GeometryCollectionSamplingNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformPointSamplingDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FNonUniformPointSamplingDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVertexWeightedPointSamplingDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFilterPointSetWithMeshDataflowNode);
	}
}

FFilterPointSetWithMeshDataflowNode::FFilterPointSetWithMeshDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&TargetMesh);
	RegisterInputConnection(&SamplePoints);
	RegisterInputConnection(&bKeepInside).SetCanHidePin(true).SetPinIsHidden(true);
	RegisterInputConnection(&WindingThreshold).SetCanHidePin(true).SetPinIsHidden(true);
	RegisterInputConnection(&MinDistance).SetCanHidePin(true).SetPinIsHidden(true);
	RegisterInputConnection(&MaxDistance).SetCanHidePin(true).SetPinIsHidden(true);
	RegisterInputConnection(&bUseSignedDistance).SetCanHidePin(true).SetPinIsHidden(true);

	RegisterOutputConnection(&SamplePoints);
}

void FFilterPointSetWithMeshDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints))
	{
		TArray<FVector> InSamplePoints = GetValue(Context, &SamplePoints);
		const double InWindingThreshold = (double)GetValue(Context, &WindingThreshold);
		const bool bInKeepInside = GetValue(Context, &bKeepInside);
		const double InMinDistance = (double)GetValue(Context, &MinDistance);
		const double InMaxDistance = (double)GetValue(Context, &MaxDistance);
		const bool bInUseSignedDistance = GetValue(Context, &bUseSignedDistance);

		if (TObjectPtr<const UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			using namespace UE::Geometry;
			InTargetMesh->ProcessMesh(
				[&InSamplePoints, InWindingThreshold, bInKeepInside, InMinDistance, InMaxDistance, bInUseSignedDistance, this]
				(const FDynamicMesh3& Mesh)
				{
					const bool bFilterWinding = bool(FilterMethod & (uint8)EFilterPointSetWithMeshDataflowMethodFlags::Winding);
					const bool bFilterMinDist = bool(FilterMethod & (uint8)EFilterPointSetWithMeshDataflowMethodFlags::MinDistance);
					const bool bFilterMaxDist = bool(FilterMethod & (uint8)EFilterPointSetWithMeshDataflowMethodFlags::MaxDistance);

					if (!bFilterWinding && !bFilterMinDist && !bFilterMaxDist)
					{
						// No filtering, early-out
						return;
					}

					// Clamp unsigned distances to 0
					double UseMinDistance = InMinDistance;
					double UseMaxDistance = InMaxDistance;
					if (!bInUseSignedDistance)
					{
						UseMinDistance = FMath::Max(0, UseMinDistance);
						UseMaxDistance = FMath::Max(0, UseMaxDistance);
					}

					if (bFilterMinDist && bFilterMaxDist)
					{
						// If Min > Max, impossible for points to pass the filter, so early-out
						if (UseMinDistance > UseMaxDistance)
						{
							InSamplePoints.Empty();
							return;
						}
					}

					const bool bNeedWinding = bFilterWinding || bInUseSignedDistance;
					const bool bNeedDistance = bFilterMinDist || bFilterMaxDist;
					const double MaxRelevantDistance = UE_DOUBLE_KINDA_SMALL_NUMBER + FMath::Max(
						bFilterMinDist ? FMath::Abs(UseMinDistance) : 0,
						bFilterMaxDist ? FMath::Abs(UseMaxDistance) : 0);

					//  set up AABBTree and FWNTree lists
					TMeshAABBTree3<FDynamicMesh3> Spatial(&Mesh);
					TFastWindingTree<FDynamicMesh3> FastWinding(&Spatial, bNeedWinding);

					// square threshold distances, but keep their signs, so we can do threshold testing w/out sqrt
					const double UseSignSqMinDist = FMath::CopySign(UseMinDistance * UseMinDistance, UseMinDistance);
					const double UseSignSqMaxDist = FMath::CopySign(UseMaxDistance * UseMaxDistance, UseMaxDistance);

					// Filter points 
					TArray<bool> KeepPoint;
					KeepPoint.SetNumUninitialized(InSamplePoints.Num());
					ParallelFor(KeepPoint.Num(),
						[&KeepPoint, &Spatial, &FastWinding, &InSamplePoints, 
						bFilterWinding, bFilterMinDist, bFilterMaxDist, bInUseSignedDistance, bInKeepInside,
						bNeedWinding, InWindingThreshold, bNeedDistance, UseSignSqMinDist, UseSignSqMaxDist, MaxRelevantDistance]
						(int32 PointIdx)
						{
							FVector Pt = InSamplePoints[PointIdx];

							bool bWindingInside = false;
							if (bNeedWinding)
							{
								bWindingInside = FastWinding.IsInside(Pt, InWindingThreshold);
								// test if we fail the winding filter
								if (bFilterWinding && bInKeepInside != bWindingInside)
								{
									KeepPoint[PointIdx] = false;
									return;
								}
							}
							double FoundDistSq = 0;
							if (bNeedDistance)
							{
								const int32 FoundTID = Spatial.FindNearestTriangle(Pt, FoundDistSq, UE::Geometry::IMeshSpatial::FQueryOptions(MaxRelevantDistance));
								if (FoundTID == INDEX_NONE)
								{
									// we have a max dist, lack of closest point -> we fail the max dist filter
									// or it's inside the shape w/ signed distances -> it's too far inside, fail the min filter
									if (bFilterMaxDist || (bInUseSignedDistance && bWindingInside))
									{
										KeepPoint[PointIdx] = false;
										return;
									}
									else
									{
										// point at least passes the min distance threshold
										FoundDistSq = UseSignSqMinDist;
									}
								}
								else
								{
									if (bInUseSignedDistance && bWindingInside)
									{
										// sign the squared distance
										FoundDistSq = -FoundDistSq;
									}
								}
								if (bFilterMinDist && FoundDistSq < UseSignSqMinDist)
								{
									KeepPoint[PointIdx] = false;
									return;
								}
								else if (bFilterMaxDist && FoundDistSq > UseSignSqMaxDist)
								{
									KeepPoint[PointIdx] = false;
									return;
								}
							}

							// passed all filters, keep the point
							KeepPoint[PointIdx] = true;
						}
					);

					// Move the points we're keeping to the front of the array, and trim the array
					int32 FoundPoints = 0;
					for (int32 Idx = 0; ; ++Idx)
					{
						while (Idx < InSamplePoints.Num() && !KeepPoint[Idx])
						{
							Idx++;
						}
						if (Idx < InSamplePoints.Num())
						{
							if (Idx != FoundPoints)
							{
								InSamplePoints[FoundPoints] = InSamplePoints[Idx];
							}
							FoundPoints++;
						}
						else
						{
							break;
						}
					}
					InSamplePoints.SetNum(FoundPoints);
				}
			);
		}
		SetValue(Context, MoveTemp(InSamplePoints), &SamplePoints);
	}
}

void FUniformPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) || 
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&SampleBarycentricCoords) ||
		Out->IsA(&NumSamplePoints))
	{
		TArray<FVector> OutPoints;
		TArray<FTransform> OutSamples;
		TArray<int32> OutTriangleIDs;
		TArray<FVector> OutBarycentricCoords;

		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				FFractureEngineSampling::ComputeUniformPointSampling(InDynTargetMesh,
					GetValue(Context, &SamplingRadius),
					GetValue(Context, &MaxNumSamples),
					GetValue(Context, &SubSampleDensity),
					GetValue(Context, &RandomSeed),
					OutSamples, 
					OutTriangleIDs, 
					OutBarycentricCoords);

				const int32 NumSamples = OutSamples.Num();

				OutPoints.AddUninitialized(NumSamples);

				for (int32 Idx = 0; Idx < NumSamples; ++Idx)
				{
					OutPoints[Idx] = OutSamples[Idx].GetTranslation();
				}
			}
		}

		SetValue(Context, MoveTemp(OutPoints), &SamplePoints);
		SetValue(Context, MoveTemp(OutTriangleIDs), &SampleTriangleIDs);
		SetValue(Context, MoveTemp(OutBarycentricCoords), &SampleBarycentricCoords);
		SetValue(Context, OutPoints.Num(), &NumSamplePoints);
	}
}

void FNonUniformPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) ||
		Out->IsA(&SampleRadii) ||
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&SampleBarycentricCoords) ||
		Out->IsA(&NumSamplePoints))
	{
		TArray<FVector> OutPoints;
		TArray<FTransform> OutSamples;
		TArray<float> OutSampleRadii;
		TArray<int32> OutTriangleIDs;
		TArray<FVector> OutBarycentricCoords;

		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				FFractureEngineSampling::ComputeNonUniformPointSampling(InDynTargetMesh,
					GetValue(Context, &SamplingRadius),
					GetValue(Context, &MaxNumSamples),
					GetValue(Context, &SubSampleDensity),
					GetValue(Context, &RandomSeed),
					GetValue(Context, &MaxSamplingRadius),
					SizeDistribution,
					GetValue(Context, &SizeDistributionPower),
					OutSamples,
					OutSampleRadii,
					OutTriangleIDs, 
					OutBarycentricCoords);

				const int32 NumSamples = OutSamples.Num();

				OutPoints.AddUninitialized(NumSamples);

				for (int32 Idx = 0; Idx < NumSamples; ++Idx)
				{
					OutPoints[Idx] = OutSamples[Idx].GetTranslation();
				}
			}
		}

		SetValue(Context, MoveTemp(OutPoints), &SamplePoints);
		SetValue(Context, MoveTemp(OutSampleRadii), &SampleRadii);
		SetValue(Context, MoveTemp(OutTriangleIDs), &SampleTriangleIDs);
		SetValue(Context, MoveTemp(OutBarycentricCoords), &SampleBarycentricCoords);
		SetValue(Context, OutPoints.Num(), &NumSamplePoints);
	}
}

void FVertexWeightedPointSamplingDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&SamplePoints) ||
		Out->IsA(&SampleRadii) ||
		Out->IsA(&SampleTriangleIDs) ||
		Out->IsA(&SampleBarycentricCoords) ||
		Out->IsA(&NumSamplePoints))
	{
		TArray<FVector> OutPoints;
		TArray<FTransform> OutSamples;
		TArray<float> OutSampleRadii;
		TArray<int32> OutTriangleIDs;
		TArray<FVector> OutBarycentricCoords;

		if (TObjectPtr<UDynamicMesh> InTargetMesh = GetValue(Context, &TargetMesh))
		{
			const UE::Geometry::FDynamicMesh3& InDynTargetMesh = InTargetMesh->GetMeshRef();

			if (InDynTargetMesh.VertexCount() > 0)
			{
				if (IsConnected(&VertexWeights))
				{
					FFractureEngineSampling::ComputeVertexWeightedPointSampling(InDynTargetMesh,
						GetValue(Context, &VertexWeights),
						GetValue(Context, &SamplingRadius),
						GetValue(Context, &MaxNumSamples),
						GetValue(Context, &SubSampleDensity),
						GetValue(Context, &RandomSeed),
						GetValue(Context, &MaxSamplingRadius),
						SizeDistribution,
						GetValue(Context, &SizeDistributionPower),
						WeightMode,
						bInvertWeights,
						OutSamples,
						OutSampleRadii,
						OutTriangleIDs,
						OutBarycentricCoords);

					const int32 NumSamples = OutSamples.Num();

					OutPoints.AddUninitialized(NumSamples);

					for (int32 Idx = 0; Idx < NumSamples; ++Idx)
					{
						OutPoints[Idx] = OutSamples[Idx].GetTranslation();
					}

				}
			}
		}

		SetValue(Context, MoveTemp(OutPoints), &SamplePoints);
		SetValue(Context, MoveTemp(OutSampleRadii), &SampleRadii);
		SetValue(Context, MoveTemp(OutTriangleIDs), &SampleTriangleIDs);
		SetValue(Context, MoveTemp(OutBarycentricCoords), &SampleBarycentricCoords);
		SetValue(Context, OutPoints.Num(), &NumSamplePoints);
	}
}


