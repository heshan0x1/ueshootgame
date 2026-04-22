// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVFoliage.h"

#include "ProceduralVegetationModule.h"
#include "PVFloatRamp.h"
#include "Algo/RandomShuffle.h"

#include "Facades/PVAttributesNames.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVBudVectorsFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPointFacade.h"

struct ConditionAttributes
{
	TArray<float> UpAlignment;
	TArray<float> Light;
	TArray<float> Scale;
	TArray<float> Tip;
	TArray<float> Health;
	

	float GetConditionValue(EFoliageDistributionCondition InCondition, const int Index) const
	{
		TArray<float> Result;
		switch (InCondition)
		{
		case EFoliageDistributionCondition::Light:
			{
				Result = Light;
				break;
			}
		case EFoliageDistributionCondition::Scale:
			{
				Result = Scale;
				break;;
			}
		case EFoliageDistributionCondition::Tip:
			{
				Result = Tip;
				break;
			}
		case EFoliageDistributionCondition::UpAlignment:
			{
				Result = UpAlignment;
				break;
			}
		case EFoliageDistributionCondition::Health:
			{
				Result = Health;
				break;
			}
		}

		if (Result.IsValidIndex(Index))
		{
			return Result[Index];
		}

		return 0;
	}
};

// Condition Weight
struct FFoliageWeightInfo
{
	int FoliageIndex = 0;
	float Weight = 0;
};

void FPVFoliage::DistributeFoliage(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
                                   const FDistributionSettings& DistributionSettings, const FScaleSettings& ScaleSettings,
                                   const FVectorSettings& VectorSettings, const FPhyllotaxySettings& PhyllotaxySettings,
                                   const FMiscSettings& MiscSettings, const FPVFoliageDistributionConditions& DistributionConditions)
{
	PV::Facades::FFoliageFacade FoliageFacadeOutput(OutCollection);

	if (const int32 NumFoliageMeshes = FoliageFacadeOutput.NumFoliageInfo(); NumFoliageMeshes == 0)
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("There are no foliage meshes available in input for distribution"));
		return;
	}

	FRandomStream RandomStream;
	RandomStream.Initialize(MiscSettings.RandomSeed);

	PV::Facades::FBranchFacade BranchFacadeSource(SourceCollection);
	PV::Facades::FPointFacade PointFacadeSource(SourceCollection);

	TManagedArrayAccessor<TArray<FVector3f>> BudDirectionAttribute(OutCollection, PV::AttributeNames::BudDirection,
		PV::GroupNames::PointGroup);
	
	TManagedArrayAccessor<float> BranchGradientsAttribute(OutCollection, PV::AttributeNames::BranchGradient,
		PV::GroupNames::PointGroup);
	
	TManagedArrayAccessor<TArray<float>> LeafPhyllotaxyAttribute(OutCollection, PV::AttributeNames::LeafPhyllotaxy,
		PV::GroupNames::DetailsGroup);
	
	TManagedArrayAccessor<TArray<float>> BudHormoneLevels(OutCollection, PV::AttributeNames::BudHormoneLevels,
		PV::GroupNames::PointGroup);

	TManagedArrayAccessor<TArray<int>> BudDevelopment(OutCollection, PV::AttributeNames::BudDevelopment,
		PV::GroupNames::PointGroup);

	float PhyllotaxyFormationLeaf = LeafPhyllotaxyAttribute[0][1];
	float MinBudsLeaf = LeafPhyllotaxyAttribute[0][3];
	float MaxBudsLeaf = LeafPhyllotaxyAttribute[0][4];
	int32 ResetPhyllotaxyLeaf = static_cast<int>(LeafPhyllotaxyAttribute[0][6]);
	float PhyllotaxyOffsetLeaf = LeafPhyllotaxyAttribute[0][7];

	if (PhyllotaxySettings.OverridePhyllotaxy)
	{
		switch (PhyllotaxySettings.PhyllotaxyType)
		{
		case EPhyllotaxyType::Alternate: PhyllotaxyFormationLeaf = 180 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 1;
			break;
		case EPhyllotaxyType::Opposite: PhyllotaxyFormationLeaf = PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 2;
			break;
		case EPhyllotaxyType::Decussate: PhyllotaxyFormationLeaf = 90 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = MaxBudsLeaf = 2;
			break;
		case EPhyllotaxyType::Whorled: PhyllotaxyFormationLeaf = 90 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
			MinBudsLeaf = PhyllotaxySettings.MinimumNodeBuds;
			MaxBudsLeaf = PhyllotaxySettings.MaximumNodeBuds;
			break;
		case EPhyllotaxyType::Spiral:
			{
				// These leaf arrangement configurations where Distichous is two leaves, Tristichous is three etc.
				switch (PhyllotaxySettings.PhyllotaxyFormation)
				{
				case EPhyllotaxyFormation::Distichous: PhyllotaxyFormationLeaf = 180 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Tristichous: PhyllotaxyFormationLeaf = 120 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Pentastichous: PhyllotaxyFormationLeaf = 144 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Octastichous: PhyllotaxyFormationLeaf = 135 + PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				case EPhyllotaxyFormation::Parastichous: PhyllotaxyFormationLeaf = PhyllotaxySettings.PhyllotaxyAdditionalAngle;
					break;
				}

				MinBudsLeaf = MaxBudsLeaf = 1;
				break;
			}
		}
	}

	const int32 NumBranches = BranchFacadeSource.GetElementCount();

	if (DistributionSettings.OverrideDistribution)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PVFoliageDistributorNode::OverrideDistribution);

		TArray<float> NormalisedPScales;
		NormalisedPScales.Init(0, PointFacadeSource.GetElementCount());

		TArray<float> NormalisedBudLightDetected;
		NormalisedBudLightDetected.Init(0, PointFacadeSource.GetElementCount());

		TArray<float> NormalisedHealth;
		NormalisedHealth.Init(0, PointFacadeSource.GetElementCount());

		TArray<PV::Facades::FFoliageEntryData> FoliageEntries;

		// TODO : Optimize
		// if (DistributionConditions.IsActiveCondition(EFoliageDistributionCondition::Scale) || DistributionConditions.IsActiveCondition(EFoliageDistributionCondition::Light))
		// {
		float MinPScale = std::numeric_limits<float>::max();
		float MaxPScale = std::numeric_limits<float>::min();
			
		float MinBudLightDetected = std::numeric_limits<float>::max();
		float MaxBudLightDetected = std::numeric_limits<float>::min();

		float MinHealth = std::numeric_limits<float>::max();
		float MaxHealth = std::numeric_limits<float>::min();

		for (int PointIndex = 0; PointIndex < PointFacadeSource.GetElementCount(); PointIndex++)
		{
			// Scale
			float PScale = PointFacadeSource.GetPointScale(PointIndex);
			MinPScale = FMath::Min(MinPScale, PScale);	
			MaxPScale = FMath::Max(MaxPScale, PScale);

			// Light
			const TArray<float>& CurrentBudLightDetected = PointFacadeSource.GetBudLightDetected(PointIndex);
			const float CurrentLightAvailable = CurrentBudLightDetected.IsValidIndex(PV::Facades::BudLightDetectedAvailableIndex) ? CurrentBudLightDetected[PV::Facades::BudLightDetectedAvailableIndex] : 0.0f;
				
			MinBudLightDetected = FMath::Min(MinBudLightDetected, CurrentLightAvailable);	
			MaxBudLightDetected = FMath::Max(MaxBudLightDetected, CurrentLightAvailable);

			// Health
			float Ethylene = BudHormoneLevels[PointIndex][4];
			int BudAgeSenescence = BudDevelopment[PointIndex][3];
			int BudLightSenescence = BudDevelopment[PointIndex][4];
			
			int Senescence = FMath::Max(BudAgeSenescence,BudLightSenescence) + 1;
			NormalisedHealth[PointIndex] = 1 - (Ethylene * (1/Senescence));
			
			MinHealth = FMath::Min(MinHealth, NormalisedHealth[PointIndex]);
			MaxHealth = FMath::Max(MaxHealth, NormalisedHealth[PointIndex]);
		}

		for (int PointIndex = 0; PointIndex < PointFacadeSource.GetElementCount(); PointIndex++)
		{
			// Scale
			float PScale = PointFacadeSource.GetPointScale(PointIndex);
			NormalisedPScales[PointIndex] = FMath::GetMappedRangeValueClamped(FVector2f(MinPScale, MaxPScale),FVector2f(0, 1),PScale);

			// UE_LOG(LogTemp, Log, TEXT("Normalized PScale : Index : %i : Value : %f"),
			// 					PointIndex, NormalisedPScales[PointIndex]);

			// Light
			const TArray<float>& CurrentBudLightDetected = PointFacadeSource.GetBudLightDetected(PointIndex);
			const float CurrentLightAvailable = CurrentBudLightDetected.IsValidIndex(PV::Facades::BudLightDetectedAvailableIndex) ? CurrentBudLightDetected[PV::Facades::BudLightDetectedAvailableIndex] : 0.0f;
			NormalisedBudLightDetected[PointIndex] = FMath::GetMappedRangeValueClamped(FVector2f(MinBudLightDetected, MaxBudLightDetected),FVector2f(0, 1), CurrentLightAvailable);

			// Health
			NormalisedHealth[PointIndex] = FMath::GetMappedRangeValueClamped(FVector2f(MinHealth, MaxHealth),FVector2f(0, 1),NormalisedHealth[PointIndex]);
		}
		// }
		
		// Compute attachment points in terms of normalized distances
		OutCollection.EmptyGroup(PV::GroupNames::FoliageGroup);
 
		// Collect non-empty mesh name indices
		const TArray<FString>& FoliageMeshNames = FoliageFacadeOutput.GetFoliageNames();
		TArray<int32> NonEmptyFoliageMeshNameIndices;
		NonEmptyFoliageMeshNameIndices.Reserve(FoliageMeshNames.Num());
 
		for (int32 i = 0; i < FoliageMeshNames.Num(); ++i)
		{
			if (!FoliageMeshNames[i].IsEmpty())
			{
				NonEmptyFoliageMeshNameIndices.Add(i);
			}
		}
 
		if (NonEmptyFoliageMeshNameIndices.Num() == 0)
		{
			return;
		}
		
		for (int32 BranchIndex = 0; BranchIndex < NumBranches; ++BranchIndex)
		{
			ConditionAttributes ConditionAttributes;
			TArray<float> NormalizedAttachmentPoints;
			TArray<float> NormalizedAttachmentPointsFinal;
			TArray<FVector3f> AttachmentPointsPositions;
			TArray<float> AttachmentPointsLengthsFromRoot;
			TArray<FVector3f> AttachmentPointsUpVectors;
			TArray<FVector3f> AttachmentPointsNormalVectors;
			TArray<float> AttachmentPointsScales;
			
			const int32 NumFoliageInfo = FoliageFacadeOutput.NumFoliageInfo();
			
			TArray<int32> BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);

			if (BranchPoints.Num() < 2)
			{
				continue;
			}
			
			const float FirstPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[0]);
			const float LastPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints.Last());
			const float BranchLength = LastPointLengthFromRoot - FirstPointLengthFromRoot;
			float Increment = DistributionSettings.InstanceSpacing / BranchLength;
			const float AdjustedMaxPerBranch = DistributionSettings.MaxPerBranch == -1
				? BranchPoints.Num()
				: DistributionSettings.MaxPerBranch;
			float LookUp = 0.0f;

			for (int32 i = 0; i < AdjustedMaxPerBranch; ++i)
			{
				if (LookUp > 1.0f)
				{
					break;
				}

				float InstanceSpacingRampValue = DistributionSettings.InstanceSpacingRamp
					? DistributionSettings.InstanceSpacingRamp->GetRichCurveConst()->Eval(1.0f - LookUp)
					: Increment;
				// The increment is nudged slightly before blending with the weighted ramp value, the number is chosen based on observation
				float AdjustedIncrement = Increment + (InstanceSpacingRampValue * (Increment * 10.0f));
				AdjustedIncrement = FMath::Lerp(Increment, AdjustedIncrement, DistributionSettings.InstanceSpacingRampEffect);

				NormalizedAttachmentPoints.Add(1.0f - LookUp);
				LookUp += AdjustedIncrement;
			}

			// Compute Position, Angles, Scale and LengthFromRoot for attachment points

			// Compute based on first point attributes
			const FVector3f InitialApicalDirection(BudDirectionAttribute[BranchPoints[0]][0]);
			FVector3f InitialAxillaryDirection(BudDirectionAttribute[BranchPoints[0]][1]);
			const FVector3f InitialUpVector(BudDirectionAttribute[BranchPoints[0]][5]);

			if (ResetPhyllotaxyLeaf == 1)
			{
				InitialAxillaryDirection = FVector3f::CrossProduct(InitialApicalDirection, InitialUpVector);
			}

			if (PhyllotaxyOffsetLeaf > 0.01f)
			{
				const FQuat4f RotationQuat(InitialApicalDirection.GetSafeNormal(), FMath::DegreesToRadians(PhyllotaxyOffsetLeaf));
				InitialAxillaryDirection = RotationQuat.RotateVector(InitialAxillaryDirection);
			}

			int32 AxillaryRotationIteration = 0;
			for (int32 Idx = 0; Idx < NormalizedAttachmentPoints.Num(); ++Idx)
			{
				for (int32 BranchPointIndex = 0; BranchPointIndex < BranchPoints.Num() - 1; ++BranchPointIndex)
				{
					float AxillaryRotationDegree = FMath::DegreesToRadians(PhyllotaxyFormationLeaf * AxillaryRotationIteration);

					const int32 CurrentPointIndex = BranchPoints[BranchPointIndex];
					const int32 NextPointIndex = BranchPoints[BranchPointIndex + 1];

					const FVector3f CurrentPointPosition = PointFacadeSource.GetPosition(CurrentPointIndex);
					const FVector3f NextPointPosition = PointFacadeSource.GetPosition(NextPointIndex);

					float EthyleneLevelAtCurrentPoint = BudHormoneLevels[CurrentPointIndex][4];
					float EthyleneLevelAtNextPoint = BudHormoneLevels[NextPointIndex][4];

					const float CurrentPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(CurrentPointIndex);
					const float NextPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(NextPointIndex);
					
					const float CurrentPointLfrNormalized = FMath::GetMappedRangeValueClamped(
						FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
						FVector2f(0.0f, 1.0f), CurrentPointLengthFromRoot);

					const float NextPointLfrNormalized = FMath::GetMappedRangeValueClamped(
						FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
						FVector2f(0.0f, 1.0f),
						NextPointLengthFromRoot);

					if (NormalizedAttachmentPoints[Idx] >= CurrentPointLfrNormalized && NormalizedAttachmentPoints[Idx] <=
						NextPointLfrNormalized)
					{
						const float DistanceAlpha = (NormalizedAttachmentPoints[Idx] -
							CurrentPointLfrNormalized) / (NextPointLfrNormalized - CurrentPointLfrNormalized);

						const float EthyleneLevel = FMath::Lerp(EthyleneLevelAtCurrentPoint, EthyleneLevelAtNextPoint, DistanceAlpha);

						if ((EthyleneLevel - 0.001f) >= DistributionSettings.EthyleneThreshold)
						{
							continue;
						}

						const FVector3f Position = FMath::Lerp(CurrentPointPosition, NextPointPosition, DistanceAlpha);
						AttachmentPointsPositions.Add(Position);

						const float AttachmentPointLfr = FMath::GetMappedRangeValueClamped(
							FVector2f(0.0f, 1.0f),
							FVector2f(FirstPointLengthFromRoot, LastPointLengthFromRoot),
							NormalizedAttachmentPoints[Idx]);
						AttachmentPointsLengthsFromRoot.Add(AttachmentPointLfr);

						// Compute Scale
						const float CurrentPointBranchGradient = BranchGradientsAttribute[CurrentPointIndex];
						const float NextPointBranchGradient = BranchGradientsAttribute[NextPointIndex];

						const float BranchGradient = FMath::Clamp(
							FMath::Lerp(CurrentPointBranchGradient, NextPointBranchGradient, DistanceAlpha), 0.0f, 1.0f);
						float BranchGradientRampValue = ScaleSettings.ScaleRamp
							? ScaleSettings.ScaleRamp->GetRichCurveConst()->Eval(1.0f - BranchGradient)
							: 1.0f;

						const float CurrentPointScale = PointFacadeSource.GetPointScale(CurrentPointIndex);
						const float NextPointScale = PointFacadeSource.GetPointScale(NextPointIndex);
						const float BranchScale = FMath::Lerp(CurrentPointScale, NextPointScale, DistanceAlpha);

						const float BranchScaledValue = BranchGradientRampValue * (BranchScale + 0.1f);
						BranchGradientRampValue = FMath::Lerp(BranchGradientRampValue, BranchScaledValue, ScaleSettings.BranchScaleImpact);
						BranchGradientRampValue = FMath::Clamp(BranchGradientRampValue, 0.0f, 1.0f);

						float Scale = FMath::GetMappedRangeValueClamped(
							FVector2f(0.0f, 1.0f),
							FVector2f(ScaleSettings.MinScale, ScaleSettings.MaxScale),
							BranchGradientRampValue
						) * ScaleSettings.BaseScale;

						const float RandomValue = RandomStream.FRandRange(ScaleSettings.RandomScaleMin, ScaleSettings.RandomScaleMax);
						Scale = Scale * RandomValue;

						AttachmentPointsScales.Add(Scale);
						
						const FVector3f CurrentPointApicalDirectionVector(BudDirectionAttribute[CurrentPointIndex][0]);
						const FVector3f NextPointApicalDirectionVector(BudDirectionAttribute[NextPointIndex][0]);
						const FVector3f BudApicalDirectionVector = FMath::Lerp(CurrentPointApicalDirectionVector,
							NextPointApicalDirectionVector, DistanceAlpha);

						// Condition - Y Dot
						float YDot = FVector3f::DotProduct(BudApicalDirectionVector, FVector3f::UpVector);
						YDot = FMath::GetMappedRangeValueClamped(FVector2f(-1, 1),FVector2f(0, 1),YDot);
						ConditionAttributes.UpAlignment.Add(YDot);

						// Condition - Light
						// const TArray<float>& CurrentBudLightDetected = PointFacadeSource.GetBudLightDetected(CurrentPointIndex);
						// const float CurrentLightAvailable = CurrentBudLightDetected.IsValidIndex(PV::Facades::BudLightDetectedAvailableIndex) ? CurrentBudLightDetected[PV::Facades::BudLightDetectedAvailableIndex] : 0.0f;
						//
						// const TArray<float>& NextBudLightDetected = PointFacadeSource.GetBudLightDetected(NextPointIndex);
						// const float NextLightAvailable = NextBudLightDetected.IsValidIndex(PV::Facades::BudLightDetectedAvailableIndex) ? NextBudLightDetected[PV::Facades::BudLightDetectedAvailableIndex] : 0.0f;	

						const float CurrentLightAvailable = NormalisedBudLightDetected[CurrentPointIndex];
						const float NextLightAvailable = NormalisedBudLightDetected[NextPointIndex];
						const float BudLightReceived = FMath::Lerp(CurrentLightAvailable,NextLightAvailable, DistanceAlpha);
						ConditionAttributes.Light.Add(BudLightReceived);

						// Condition - Scale
						// const float CurrentPointScaleGradient = PointFacadeSource.GetPointScaleGradient(CurrentPointIndex);
						// const float NextPointScaleGradient = PointFacadeSource.GetPointScaleGradient(NextPointIndex);
						// const float PointScaleGradient = FMath::Lerp(CurrentPointScaleGradient, NextPointScaleGradient, DistanceAlpha);

						const float CurrentPScale = NormalisedPScales[CurrentPointIndex];
						const float NextPScale = NormalisedPScales[NextPointIndex];
						const float PointPScale = FMath::Lerp(CurrentPScale, NextPScale, DistanceAlpha);
						ConditionAttributes.Scale.Add(PointPScale);

						// Health
						const float CurrentHealth = NormalisedHealth[CurrentPointIndex];
						const float NextHealth = NormalisedHealth[NextPointIndex];
						const float BudHealth = FMath::Lerp(CurrentHealth,NextHealth, DistanceAlpha);
						ConditionAttributes.Health.Add(BudHealth);

						// Condition - Tip
						if (BranchPointIndex == BranchPoints.Num() - 2 && DistanceAlpha == 1)
						{
							ConditionAttributes.Tip.Add(1);
						}
						else
						{
							ConditionAttributes.Tip.Add(0);
						}

						int32 BudsToAdd = 1;

						// Compute Up Vector
						// Last point (order of the array is last to first)
						if (Idx == 0)
						{
							AttachmentPointsUpVectors.Add(BudApicalDirectionVector);
						}
						else // Points other than last
							[[likely]]
							{
								const FQuat4f RotationQuat(InitialApicalDirection.GetSafeNormal(), AxillaryRotationDegree);
								FVector3f NewAxillaryDirection = RotationQuat.RotateVector(InitialAxillaryDirection.GetSafeNormal());
								FQuat4f ApicalCorrection = FQuat4f::FindBetweenNormals(
									InitialApicalDirection.GetSafeNormal(), BudApicalDirectionVector.GetSafeNormal());
								NewAxillaryDirection = ApicalCorrection.RotateVector(NewAxillaryDirection).GetSafeNormal();

								int32 NumBuds = RandomStream.RandRange(MinBudsLeaf, MaxBudsLeaf);
								float RotationPerBud = 360.0f / static_cast<float>(NumBuds);

								for (int32 i = 0; i < NumBuds; ++i)
								{
									float RotationAngle = FMath::DegreesToRadians(RotationPerBud * i);
									const FQuat4f AxillaryRotation(InitialApicalDirection.GetSafeNormal(), RotationAngle);
									FVector3f AxillaryDirectionModified = AxillaryRotation.RotateVector(NewAxillaryDirection.GetSafeNormal());

									if (VectorSettings.OverrideAxilAngle)
									{
										float AxilAngleRampValue = VectorSettings.AxilAngleRamp
											? FMath::Clamp(
												VectorSettings.AxilAngleRamp->GetRichCurveConst()->Eval(1.0f - BranchGradient),
												0.0f,
												1.0f)
											: 1.0f;

										AxilAngleRampValue = AxilAngleRampValue * VectorSettings.AxilAngleRampUpperValue;
										const float AxilAngleBlended = FMath::Lerp(VectorSettings.AxilAngle, AxilAngleRampValue,
											VectorSettings.AxilAngleRampEffect);

										const FVector3f RotationAxis = FVector3f::CrossProduct(
											BudApicalDirectionVector.GetSafeNormal(), AxillaryDirectionModified.GetSafeNormal());

										const FQuat4f Rotation(RotationAxis.GetSafeNormal(), FMath::DegreesToRadians(AxilAngleBlended));
										const FVector3f AxilVector = Rotation.RotateVector(BudApicalDirectionVector.GetSafeNormal());

										AttachmentPointsUpVectors.Add(AxilVector);
									}
									else
									{
										AttachmentPointsUpVectors.Add(AxillaryDirectionModified);
									}

									if (i > 0)
									{
										FVector3f LastPositionAdded = AttachmentPointsPositions.Last();
										float LastScaleAdded = AttachmentPointsScales.Last();
										float LastLengthFromRootAdded = AttachmentPointsLengthsFromRoot.Last();
										AttachmentPointsPositions.Add(LastPositionAdded);
										AttachmentPointsScales.Add(LastScaleAdded);
										AttachmentPointsLengthsFromRoot.Add(LastLengthFromRootAdded);

										// Conditions
										float LastConditionScale = ConditionAttributes.Scale.Last();
										ConditionAttributes.Scale.Add(LastConditionScale);
									
										float LastConditionLight = ConditionAttributes.Light.Last();
										ConditionAttributes.Light.Add(LastConditionLight);
									
										float LastConditionUpAlignment  = ConditionAttributes.UpAlignment.Last();
										ConditionAttributes.UpAlignment.Add(LastConditionUpAlignment);

										float LastConditionTip = ConditionAttributes.Tip.Last();
										ConditionAttributes.Tip.Add(LastConditionTip);

										float LastConditionHealth = ConditionAttributes.Health.Last();
										ConditionAttributes.Health.Add(LastConditionHealth);
									}
								}

								BudsToAdd = NumBuds;
								AxillaryRotationIteration++;
							}

						// Compute Normal vectors based on light optimal direction
						const FVector3f CurrentPointLightOptimalVector(BudDirectionAttribute[CurrentPointIndex][2]);
						const FVector3f NextPointLightOptimalVector(BudDirectionAttribute[NextPointIndex][2]);
						const FVector3f BudLightOptimalVector = FMath::Lerp(CurrentPointLightOptimalVector, NextPointLightOptimalVector,
							DistanceAlpha);

						FQuat4f QuatRotation = FQuat4f::FindBetweenNormals(BudApicalDirectionVector.GetSafeNormal(),
							BudLightOptimalVector.GetSafeNormal());
						const FVector3f ApicalDirectionRotated = QuatRotation.RotateVector(BudApicalDirectionVector);

						for (int32 i = 0; i < BudsToAdd; ++i)
						{
							AttachmentPointsNormalVectors.Add(ApicalDirectionRotated);
						}
					}
				}
			}

			// Set foliage entries
			TManagedArrayAccessor<FVector3f> PivotPointsAttribute(OutCollection, PV::AttributeNames::FoliagePivotPoint,
				PV::GroupNames::FoliageGroup);

			const TArray<FPVFoliageInfo> FoliageInfos = FoliageFacadeOutput.GetFoliageInfos();
			

			const int32 BranchNumber = BranchFacadeSource.GetBranchNumber(BranchIndex);

			FRandomStream FoliageRandomStream;
			FoliageRandomStream.Initialize(BranchNumber);
			
			for (int32 Idx = 0; Idx < AttachmentPointsLengthsFromRoot.Num(); ++Idx)
			{
				TArray<FFoliageWeightInfo> AvailableFoliageWeights;
				
				for (int i = 0; i < NumFoliageInfo; ++i)
				{
					auto FoliageInfo = FoliageInfos[i];

					if (FoliageInfo.IsValid())
					{
						AvailableFoliageWeights.Add(FFoliageWeightInfo(i, 0));
					}
				}

				// Shuffle foliage
				for (int i = AvailableFoliageWeights.Num() - 1; i > 0; --i)
				{
					int Index = RandomStream.RandRange(0, i);
					AvailableFoliageWeights.Swap(i, Index);
				}

				if(DistributionConditions.HasActiveCondition())
				{
					for (EFoliageDistributionCondition Condition : TEnumRange<EFoliageDistributionCondition>())
					{
						FPVFoliageDistributionConditionSettings OutConditionSettings;
						
						if (DistributionConditions.IsActiveCondition(Condition) && DistributionConditions.GetSettings(Condition, OutConditionSettings))
						{
							float PointAttributeValue = ConditionAttributes.GetConditionValue(Condition,Idx);
					
							const float AttributeValue = PointAttributeValue + OutConditionSettings.Offset;

							for (int Index = 0; Index < AvailableFoliageWeights.Num(); ++Index)
							{
								int FoliageIndex = AvailableFoliageWeights[Index].FoliageIndex;

								if (FoliageInfos.IsValidIndex(FoliageIndex))
								{
									auto FoliageInfo = FoliageInfos[FoliageIndex];
									float FoliageValue = FoliageInfo.Attributes.GetAttributeValue(Condition);
									float FoliageDistribution = FMath::Abs(FoliageValue - AttributeValue) * OutConditionSettings.Weight;
									
									// UE_LOG(LogProceduralVegetation, Log, TEXT("FoliageWeight : Index : %i : FoliageValue : %f : AttributeValue : %f : Weight : %f : Final Value: %f"),
									// 	FoliageIndex,
									// 	FoliageValue,
									// 	AttributeValue,
									// 	OutConditionSettings.Weight,
									// 	FoliageDistribution);	

									AvailableFoliageWeights[Index].Weight += FoliageDistribution;
								}
								else
								{
									check(false);
								}
							}	
						}
					}

					float MinFoliageWeight = Algo::MinElementBy(AvailableFoliageWeights, [](const FFoliageWeightInfo& Info) { return Info.Weight; })->Weight;
					float MaxFoliageWeight = Algo::MaxElementBy(AvailableFoliageWeights, [](const FFoliageWeightInfo& Info) { return Info.Weight; })->Weight;

					for (FFoliageWeightInfo& Info : AvailableFoliageWeights)
					{
						Info.Weight = FMath::GetMappedRangeValueClamped(
									FVector2f(MinFoliageWeight, MaxFoliageWeight),
									FVector2f(0, 1),
									Info.Weight);
					}
				}

				// Pick foliage
				AvailableFoliageWeights.Sort([&](const FFoliageWeightInfo& A, const FFoliageWeightInfo& B)
					{
						return A.Weight < B.Weight; // Ascending
					});

				
				TArray<int> PickedFoliage;

				
				for (const FFoliageWeightInfo& Info : AvailableFoliageWeights)
				{
					if (Info.Weight < DistributionConditions.CutoffThreshold)
					{
						PickedFoliage.Add(Info.FoliageIndex);
					}
					else if (PickedFoliage.Num() < DistributionConditions.MinimumCandidates)
					{
						PickedFoliage.Add(Info.FoliageIndex);
					}
					else
					{
						break;
					}
				}

				const int PickedIndex = RandomStream.RandRange(0, PickedFoliage.Num() - 1);
				int PickedFoliageIndex = PickedFoliage[PickedIndex];

				const int32 NewIndex = PivotPointsAttribute.AddElements(1);
				FoliageFacadeOutput.SetFoliageEntry(NewIndex, {
					.NameId = PickedFoliageIndex,
					.BranchId = BranchIndex,
					.PivotPoint = AttachmentPointsPositions[Idx],
					.UpVector = AttachmentPointsUpVectors[Idx],
					.NormalVector = AttachmentPointsNormalVectors[Idx],
					.Scale = AttachmentPointsScales[Idx],
					.LengthFromRoot = AttachmentPointsLengthsFromRoot[Idx],
					.ConditionUpAlignment = ConditionAttributes.UpAlignment[Idx],
					.ConditionTip = ConditionAttributes.Tip[Idx],
					.ConditionLight = ConditionAttributes.Light[Idx],
					.ConditionScale = ConditionAttributes.Scale[Idx],
					.ConditionHealth = ConditionAttributes.Health[Idx]
				});
			}
		}

		// TArray<float> Scales;
		//
		// for (int32 i = 0; i < FoliageFacadeOutput.NumFoliageEntries(); ++i)
		// {
		// 	FoliageFacadeOutput.GetFoliageEntry(i).Scale;
		// }
		
		// Re-calculate Foliage IDs for each branch
		TMap<int32, TArray<int32>> BranchIndexToFoliageIDs;
		for (int32 i = 0; i < FoliageFacadeOutput.NumFoliageEntries(); ++i)
		{
			const int32 BranchIndex = FoliageFacadeOutput.GetFoliageEntry(i).BranchId;
			TArray<int32>& IDs = BranchIndexToFoliageIDs.FindOrAdd(BranchIndex);
			IDs.Add(i);
		}
		for (const TPair<int32, TArray<int32>>& Pair : BranchIndexToFoliageIDs)
		{
			int32 BranchId = Pair.Key;
			const TArray<int32>& FoliageIDs = Pair.Value;
			FoliageFacadeOutput.SetFoliageIdsArray(BranchId, FoliageIDs);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PVFoliageDistributorNode::EthyleneRemove);

		// No need to apply Ethylene threshold if the distribution was overriden since that's already been accounted for
		if (DistributionSettings.OverrideDistribution)
		{
			return;
		}

		// Compute Foliage instances to remove based on Ethylene threshold
		TManagedArrayAccessor<FVector3f> PivotPointsAttribute(
			OutCollection,
			PV::AttributeNames::FoliagePivotPoint,
			PV::GroupNames::FoliageGroup);
		TSet<int32> FoliageEntriesToRemove;

		for (int32 BranchIndex = 0; BranchIndex < NumBranches; ++BranchIndex)
		{
			TArray<int32> BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);
			TArray<int32> FoliageIDs = FoliageFacadeOutput.GetFoliageEntryIdsForBranch(BranchIndex);
			TSet<int32> FoliageIDsToRemove;

			for (int32 BranchPointIndex = 0; BranchPointIndex < BranchPoints.Num() - 1; ++BranchPointIndex)
			{
				for (int32 FoliageIDsIndex = 0; FoliageIDsIndex < FoliageIDs.Num(); ++FoliageIDsIndex)
				{
					const float CurrentPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[BranchPointIndex]);
					const float NextPointLengthFromRoot = PointFacadeSource.GetLengthFromRoot(BranchPoints[BranchPointIndex + 1]);
					const float FoliageLengthFromRoot = FoliageFacadeOutput.GetFoliageEntry(FoliageIDs[FoliageIDsIndex]).LengthFromRoot;

					if (FoliageLengthFromRoot >= CurrentPointLengthFromRoot && FoliageLengthFromRoot <= NextPointLengthFromRoot)
					{
						float EthyleneLevelAtCurrentPoint = BudHormoneLevels[BranchPoints[BranchPointIndex]][4];
						float EthyleneLevelAtNextPoint = BudHormoneLevels[BranchPoints[BranchPointIndex + 1]][4];

						float EthyleneLevel = FMath::GetMappedRangeValueClamped(
							FVector2f(CurrentPointLengthFromRoot, NextPointLengthFromRoot),
							FVector2f(EthyleneLevelAtNextPoint, EthyleneLevelAtCurrentPoint),
							FoliageLengthFromRoot);

						if ((EthyleneLevel - 0.001f) >= DistributionSettings.EthyleneThreshold)
						{
							FoliageIDsToRemove.Add(FoliageIDs[FoliageIDsIndex]);
						}
					}
				}
			}

			for (int32 Fid : FoliageIDsToRemove)
			{
				FoliageEntriesToRemove.Add(Fid);
			}
		}

		// Remove foliage instances
		for (int32 i = PivotPointsAttribute.Num() - 1; i >= 0; --i)
		{
			if (FoliageEntriesToRemove.Contains(i))
			{
				PivotPointsAttribute.RemoveElements(1, i);
			}
		}

		// Re-calculate Foliage IDs for each branch
		TMap<int32, TArray<int32>> BranchIndexToFoliageIDs;
		for (int32 i = 0; i < FoliageFacadeOutput.NumFoliageEntries(); ++i)
		{
			const int32 BranchIndex = FoliageFacadeOutput.GetFoliageEntry(i).BranchId;
			TArray<int32>& IDs = BranchIndexToFoliageIDs.FindOrAdd(BranchIndex);
			IDs.Add(i);
		}
		for (const TPair<int32, TArray<int32>>& Pair : BranchIndexToFoliageIDs)
		{
			int32 BranchId = Pair.Key;
			const TArray<int32>& FoliageIDs = Pair.Value;
			FoliageFacadeOutput.SetFoliageIdsArray(BranchId, FoliageIDs);
		}
	}
}
