// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSplineIntersection.h"

#include "PCGContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSplineData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSplineIntersection)

#define LOCTEXT_NAMESPACE "PCGSplineIntersectionElement"

namespace PCGSplineIntersection
{
	namespace Constants
	{
		const FName OriginatingSplineIndexAttributeName = TEXT("OriginatingSplineIndex");
		const FName IntersectingSplineIndexAttributeName = TEXT("IntersectingSplineIndex");
	}

	struct InputData
	{
		void InitializeBounds(const FVector& HalfRadiusTolerance)
		{
			// Implementation note: we add half the distance tolerance to the bounds so we never need to update them after, 
			// as we'll use them for bounds-bounds tests only.
			check(Spline);
			Bounds = Spline->GetBounds().ExpandBy(HalfRadiusTolerance, HalfRadiusTolerance);

			SegmentsNum = Spline->GetNumSegments();
			SegmentBounds.SetNumUninitialized(SegmentsNum);
			for (int32 SegmentIndex = 0; SegmentIndex < SegmentBounds.Num(); ++SegmentIndex)
			{
				SegmentBounds[SegmentIndex] = Spline->SplineStruct.GetSegmentBounds(SegmentIndex).TransformBy(Spline->SplineStruct.Transform).ExpandBy(HalfRadiusTolerance, HalfRadiusTolerance);
			}
		}

		const TArray<FVector>& GetRoughSamples(int SegmentIndex, double MinDistanceThreshold, double& OutRoughSampleStep) const
		{
			check(Spline && SegmentIndex >= 0 && SegmentIndex < SegmentsNum);
			TArray<FVector>& Samples = RoughSampling.FindOrAdd(SegmentIndex);

			const FVector::FReal SegmentLength = Spline->GetSegmentLength(SegmentIndex);
			FVector::FReal RoughSampleSize = FMath::Max(SegmentLength / 100.0, MinDistanceThreshold);

			if (Samples.IsEmpty())
			{
				// Rough sampling should be in the 10-100 range.
				int NumSamples = FMath::Floor(SegmentLength / RoughSampleSize) + 1;

				Samples.SetNumUninitialized(NumSamples+1);

				const FVector::FReal DistanceAtSegmentStart = Spline->SplineStruct.GetDistanceAlongSplineAtSplinePoint(SegmentIndex);
				for (int Sample = 0; Sample <= NumSamples; ++Sample)
				{
					Samples[Sample] = Spline->SplineStruct.GetLocationAtDistanceAlongSpline(DistanceAtSegmentStart + SegmentLength * (Sample / (double)NumSamples), ESplineCoordinateSpace::World);
				}
			}

			// Note: to address scaling, we'll take the distance between two points
			OutRoughSampleStep = FMath::Max(RoughSampleSize, (Samples[1] - Samples[0]).Length());
			return Samples;
		}

		const UPCGSplineData* Spline = nullptr;
		int InputIndex = INDEX_NONE;
		FBox Bounds = FBox(EForceInit::ForceInit);
		TArray<FBox> SegmentBounds;
		mutable TMap<int, TArray<FVector>> RoughSampling;
		int SegmentsNum;
	};

	struct IntersectionPoint
	{
		const UPCGSplineData* A = nullptr;
		const InputData* InputA = nullptr;
		const UPCGSplineData* B = nullptr;
		const InputData* InputB = nullptr;

		FVector AveragePosition;
		FVector PositionA;
		FVector PositionB;
		float InputKeyA = 0.0f;
		float InputKeyB = 0.0f;

		bool IsOnSpline(const UPCGSplineData* In) const { return In == A || In == B; }
		float GetKey(const UPCGSplineData* In) const { check(In == A || In == B); return In == A ? InputKeyA : InputKeyB; }
		const UPCGSplineData* GetOtherSpline(const UPCGSplineData* In) const { check(In == A || In == B); return In == A ? B : A; }
		const InputData* GetOtherInput(const InputData* In) const { check(In == InputA || In == InputB); return In == InputA ? InputB : InputA; }
		const FVector& GetPosition(const UPCGSplineData* In) const { check(In == A || In == B); return In == A ? PositionA : PositionB; }
		FVector& GetPosition(const UPCGSplineData* In) { check(In == A || In == B); return In == A ? PositionA : PositionB; }
	};

	TArray<IntersectionPoint> CollapseColocatedIntersections(const TArray<IntersectionPoint>& InPoints, const double SqrDistanceThreshold, const UPCGSplineData* SplineFilter)
	{
		TArray<IntersectionPoint> OutPoints;
		OutPoints.Reserve(InPoints.Num());

		TArray<int> PointsWeight;
		PointsWeight.Reserve(InPoints.Num());

		for (const IntersectionPoint& Point : InPoints)
		{
			if (SplineFilter && !Point.IsOnSpline(SplineFilter))
			{
				continue;
			}

			const FVector& Position = SplineFilter ? Point.GetPosition(SplineFilter) : Point.AveragePosition;

			int MatchingPointIndex = OutPoints.IndexOfByPredicate([&Position, &SqrDistanceThreshold](const IntersectionPoint& OutPt)
			{
				return (OutPt.AveragePosition - Position).SquaredLength() <= SqrDistanceThreshold;
			});

			if (MatchingPointIndex == INDEX_NONE)
			{
				IntersectionPoint& NewPoint = OutPoints.Emplace_GetRef(Point);
				NewPoint.AveragePosition = Position; // meaning of average position will change when we are filtering on a specific spline
				PointsWeight.Add(1);
			}
			else
			{
				const FVector NewAveragePosition = ((PointsWeight[MatchingPointIndex] * OutPoints[MatchingPointIndex].AveragePosition) + Point.AveragePosition) / static_cast<double>(PointsWeight[MatchingPointIndex] + 1);
				OutPoints[MatchingPointIndex].AveragePosition = NewAveragePosition;
				++PointsWeight[MatchingPointIndex];

				// @todo_pcg: Add additional information for more intersection info
			}
		}

		return OutPoints;
	}
}

UPCGSplineIntersectionSettings::UPCGSplineIntersectionSettings()
{
	OriginatingSplineIndexAttribute.SetAttributeName(PCGSplineIntersection::Constants::OriginatingSplineIndexAttributeName);
	IntersectingSplineIndexAttribute.SetAttributeName(PCGSplineIntersection::Constants::IntersectingSplineIndexAttributeName);
}

#if WITH_EDITOR
FName UPCGSplineIntersectionSettings::GetDefaultNodeName() const
{
	return FName(TEXT("SplineIntersection"));
}

FText UPCGSplineIntersectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Spline Intersection");
}

EPCGChangeType UPCGSplineIntersectionSettings::GetChangeTypeForProperty(const FName& InPropertyName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(InPropertyName);

	if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSplineIntersectionSettings, Output))
	{
		// This can change the output pin types, so we need a structural change
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGSplineIntersectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spline);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGSplineIntersectionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (Output == EPCGSplineIntersectionOutput::IntersectionPointsOnly)
	{
		PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	}
	else
	{
		PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Spline);
	}

	return PinProperties;
}

FPCGElementPtr UPCGSplineIntersectionSettings::CreateElement() const
{
	return MakeShared<FPCGSplineIntersectionElement>();
}

bool FPCGSplineIntersectionElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSplineIntersectionElement::Execute);

	const UPCGSplineIntersectionSettings* Settings = Context->GetInputSettings<UPCGSplineIntersectionSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	TArray<PCGSplineIntersection::InputData> InputSplines;
	const FVector HalfDistanceRadius(0.5 * Settings->DistanceThreshold);
	const double SqrDistanceThreshold = FMath::Square(Settings->DistanceThreshold);

	for (int InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];
		if (const UPCGSplineData* SplineData = Cast<UPCGSplineData>(Input.Data))
		{
			PCGSplineIntersection::InputData& SplineInputData = InputSplines.Add_GetRef(PCGSplineIntersection::InputData{ .Spline = SplineData, .InputIndex = InputIndex });
			SplineInputData.InitializeBounds(HalfDistanceRadius);
		}
	}
	
	// Early out if we have no work to do.
	if (InputSplines.IsEmpty() || Settings->DistanceThreshold <= 0)
	{
		return true;
	}

	// Compute intersections...
	TArray<PCGSplineIntersection::IntersectionPoint> Intersections;

	TArray<int> SplinesToTest;
	for (int SplineIndex = 0; SplineIndex < InputSplines.Num(); ++SplineIndex)
	{
		SplinesToTest.Reset();

		if (Settings->Type == EPCGSplineIntersectionType::Self)
		{
			SplinesToTest.Add(SplineIndex);
		}
		else
		{
			for (int OtherSplineIndex = SplineIndex + 1; OtherSplineIndex < InputSplines.Num(); ++OtherSplineIndex)
			{
				SplinesToTest.Add(OtherSplineIndex);
			}
		}

		const PCGSplineIntersection::InputData& CurrentSplineData = InputSplines[SplineIndex];
		const FBox& CurrentBounds = CurrentSplineData.Bounds;
		
		for (int SplineToTest : SplinesToTest)
		{
			const PCGSplineIntersection::InputData& OtherSplineData = InputSplines[SplineToTest];
			const FBox& OtherBounds = OtherSplineData.Bounds;
			const bool bIsSameData = (OtherSplineData.Spline == CurrentSplineData.Spline);

			// Early test
			if (!CurrentBounds.Intersect(OtherBounds))
			{
				continue;
			}

			// Test on a segment-segment basis
			for (int32 SegmentIndex = 0; SegmentIndex < CurrentSplineData.SegmentBounds.Num(); ++SegmentIndex)
			{
				const FBox& CurrentSegmentBounds = CurrentSplineData.SegmentBounds[SegmentIndex];

				// First early out if the segment does not intersect with the other spline bounds
				if (!CurrentSegmentBounds.Intersect(OtherBounds))
				{
					continue;
				}

				double CurrentRoughStep = 0.0;
				const TArray<FVector>& CurrentSamples = CurrentSplineData.GetRoughSamples(SegmentIndex, Settings->DistanceThreshold, CurrentRoughStep);

				// Then iterate through potential segments
				for (int32 OtherSegmentIndex = 0; OtherSegmentIndex < OtherSplineData.SegmentBounds.Num(); ++OtherSegmentIndex)
				{
					// Test against bounds first
					if (!CurrentSegmentBounds.Intersect(OtherSplineData.SegmentBounds[OtherSegmentIndex]))
					{
						continue;
					}

					double OtherRoughStep = 0.0;
					const TArray<FVector>& OtherSamples = OtherSplineData.GetRoughSamples(OtherSegmentIndex, Settings->DistanceThreshold, OtherRoughStep);
					
					//const double RoughToleranceSqr = 0.25 * (FMath::Square(CurrentRoughStep) + FMath::Square(OtherRoughStep));

					// At this point, we are in intersection-possible territory.
					// We don't have an analytical solution here, but there are a few things to consider:
					// It's possible that two segments intersect in more than one place (up to 4 assumedly),
					// but it's also possible that segments are collinear.
					for (int CurrentSampleIndex = 0; CurrentSampleIndex < CurrentSamples.Num() - 1; ++CurrentSampleIndex)
					{
						const FVector& A = CurrentSamples[CurrentSampleIndex];
						const FVector& B = CurrentSamples[CurrentSampleIndex + 1];

						const int OtherStartSampleIndex = bIsSameData ? CurrentSampleIndex + 1 : 0;
						for (int OtherSampleIndex = OtherStartSampleIndex; OtherSampleIndex < OtherSamples.Num() - 1; ++OtherSampleIndex)
						{
							const FVector& C = OtherSamples[OtherSampleIndex];
							const FVector& D = OtherSamples[OtherSampleIndex + 1];

							// This is an approximation because we linearized the spline segment.
							// If this isn't enough, there are a few solutions here:
							// - Make the discretization size configurable
							// - Change the threshold here so we incorporate the approximate difference
							// .. and eventually resplit said segment
							FVector X, Y;
							// @todo_pcg: might want to use the safe version if we have colocated control points
							FMath::SegmentDistToSegment(A, B, C, D, X, Y);

							if ((X - Y).SquaredLength() > SqrDistanceThreshold)
							{
								continue;
							}

							const float CurrentKey = CurrentSplineData.Spline->SplineStruct.FindInputKeyOnSegmentClosestToWorldLocation(X, SegmentIndex);
							const FVector ClosestPointOnCurrentSpline = CurrentSplineData.Spline->SplineStruct.GetLocationAtSplineInputKey(CurrentKey, ESplineCoordinateSpace::World);

							const float OtherKey = OtherSplineData.Spline->SplineStruct.FindInputKeyOnSegmentClosestToWorldLocation(Y, OtherSegmentIndex);
							const FVector ClosestPointOnOtherSpline = OtherSplineData.Spline->SplineStruct.GetLocationAtSplineInputKey(OtherKey, ESplineCoordinateSpace::World);

							// @todo_pcg: Validate distance after?
							const FVector MidPoint = (ClosestPointOnCurrentSpline + ClosestPointOnOtherSpline) * 0.5;
							PCGSplineIntersection::IntersectionPoint& Intersection = Intersections.Emplace_GetRef();
							Intersection.A = CurrentSplineData.Spline;
							Intersection.InputA = &CurrentSplineData;
							Intersection.B = OtherSplineData.Spline;
							Intersection.InputB = &OtherSplineData;
							Intersection.PositionA = ClosestPointOnCurrentSpline;
							Intersection.PositionB = ClosestPointOnOtherSpline;
							Intersection.AveragePosition = MidPoint;
							Intersection.InputKeyA = CurrentKey;
							Intersection.InputKeyB = OtherKey;
						}
					}
				}
			}
		}
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	auto LogErrorOnIndexAttributeCreation = [Context](const FName IndexName)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("FailedCreateIndexAttribute", "Failed to create the index attribute '{0}'."), FText::FromName(IndexName)), Context);
	};

	// Finally, build output data.
	if (Settings->Output == EPCGSplineIntersectionOutput::IntersectionPointsOnly)
	{
		// Start by filtering out points that are too close to each other.
		TArray<PCGSplineIntersection::IntersectionPoint> FilteredIntersections = PCGSplineIntersection::CollapseColocatedIntersections(Intersections, SqrDistanceThreshold, nullptr);

		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		
		UPCGBasePointData* PointData = FPCGContext::NewPointData_AnyThread(Context);
		Output.Data = PointData;

		PointData->SetNumPoints(FilteredIntersections.Num());

		const EPCGPointNativeProperties PropertiesToAllocate = EPCGPointNativeProperties::Transform | (Settings->bOutputSplineIndices ? EPCGPointNativeProperties::MetadataEntry : EPCGPointNativeProperties::None);
		PointData->AllocateProperties(PropertiesToAllocate);

		FPCGMetadataAttribute<int32>* FirstIndexAttribute = nullptr;
		FPCGMetadataAttribute<int32>* SecondIndexAttribute = nullptr;

		if (Settings->bOutputSplineIndices)
		{
			auto CreateIndexAttribute = [PointData, &LogErrorOnIndexAttributeCreation](const FName IndexName) -> FPCGMetadataAttribute<int32>*
			{
				FPCGMetadataAttribute<int32>* IndexAttribute = PointData->MutableMetadata()->FindOrCreateAttribute<int32>(IndexName, -1, /*bAllowInterpolation=*/false);
				if (!IndexAttribute)
				{
					LogErrorOnIndexAttributeCreation(IndexName);
				}

				return IndexAttribute;
			};

			FirstIndexAttribute = CreateIndexAttribute(Settings->OriginatingSplineIndexAttribute.GetAttributeName());
			SecondIndexAttribute = CreateIndexAttribute(Settings->IntersectingSplineIndexAttribute.GetAttributeName());
		}

		FPCGPointValueRanges OutRanges(PointData, /*bAllocate=*/false);

		for (int Index = 0; Index < FilteredIntersections.Num(); ++Index)
		{
			const PCGSplineIntersection::IntersectionPoint& Point = FilteredIntersections[Index];
			OutRanges.TransformRange[Index] = FTransform(Point.AveragePosition);

			if (FirstIndexAttribute || SecondIndexAttribute)
			{
				PCGMetadataEntryKey& CurrentEntryKey = OutRanges.MetadataEntryRange[Index];
				PointData->Metadata->InitializeOnSet(CurrentEntryKey);

				if (FirstIndexAttribute)
				{
					FirstIndexAttribute->SetValue(CurrentEntryKey, Point.InputA->InputIndex);
				}

				if (SecondIndexAttribute)
				{
					SecondIndexAttribute->SetValue(CurrentEntryKey, Point.InputB->InputIndex);
				}
			}
		}
	}
	else if (Settings->Output == EPCGSplineIntersectionOutput::OriginalSplinesWithIntersections)
	{
		for (const PCGSplineIntersection::InputData& InputData : InputSplines)
		{
			const UPCGSplineData* InputSpline = InputData.Spline;

			// Filter points for the current spline and collapse points that would be too close.
			// Note: the average position in this case will be when points are collapsed on the spline itself, not against the other splines
			TArray<PCGSplineIntersection::IntersectionPoint> CollapsedPoints = PCGSplineIntersection::CollapseColocatedIntersections(Intersections, SqrDistanceThreshold, InputSpline);
			// Sort by key
			CollapsedPoints.Sort([InputSpline](const auto& A, const auto& B) { return A.GetKey(InputSpline) < B.GetKey(InputSpline); });

			// @todo_pcg : we should not add points that are already on existing control points,
			// but we should mark them as intersections

			// At this point, we've successfully filtered/collapsed points if it was needed,
			// Or we had no new points - in which case we have nothing to do.
			FPCGTaggedData& Output = Outputs.Add_GetRef(Inputs[InputData.InputIndex]);
			if (!CollapsedPoints.IsEmpty())
			{
				UPCGSplineData* NewSplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
				NewSplineData->InitializeFromData(InputData.Spline);

				// implementation note: this does not support typecasting, will just overwrite the attribute.
				FPCGMetadataAttribute<int32>* IndexAttribute = nullptr;

				if (Settings->bOutputSplineIndices)
				{
					FName IndexAttributeName = Settings->IntersectingSplineIndexAttribute.GetAttributeName();
					IndexAttribute = NewSplineData->MutableMetadata()->FindOrCreateAttribute(IndexAttributeName, -1, /*bAllowInterpolation=*/false);

					if (!IndexAttribute)
					{
						LogErrorOnIndexAttributeCreation(IndexAttributeName);
					}
				}

				// Implementation note: with the old spline representation, it's not possible to add control points in between other points, 
				// because there are some places in the code where we rely on the index being equal to the key (esp. around the ReparamTable).
				// Not respecting this makes some operations fail, so we need to conform to that expectation at this point in time.
				// However, this means we basically need to have custom tangents everywhere.
				TArray<FSplinePoint> SplinePoints = InputData.Spline->GetSplinePoints();
				TArray<int64> SplinePointsEntryKeys = InputData.Spline->GetMetadataEntryKeysForSplinePoints();
				check(SplinePoints.Num() == SplinePointsEntryKeys.Num() || SplinePointsEntryKeys.IsEmpty());
				const bool bHasMetadata = !SplinePointsEntryKeys.IsEmpty();
				const bool bNeedsMetadata = (IndexAttribute != nullptr);

				// Prepare metadata entry keys if we're going to use them later
				if (bNeedsMetadata && !bHasMetadata)
				{
					SplinePointsEntryKeys.SetNumUninitialized(SplinePoints.Num());
					for (int64& SplinePointEntryKey : SplinePointsEntryKeys)
					{
						SplinePointEntryKey = PCGInvalidEntryKey;
					}
				}

				auto OptionalWriteIntersectionMetadata = [bNeedsMetadata, NewSplineData, IndexAttribute, &InputData](int64& EntryKey, const PCGSplineIntersection::IntersectionPoint& IntersectionPoint)
				{
					if (bNeedsMetadata)
					{
						NewSplineData->Metadata->InitializeOnSet(EntryKey);

						if (IndexAttribute)
						{
							IndexAttribute->SetValue(EntryKey, IntersectionPoint.GetOtherInput(&InputData)->InputIndex);
						}
					}
				};
				
				const float MinKeyDifferenceThreshold = 1.0e-4f;
				
				for (const PCGSplineIntersection::IntersectionPoint& PointToAdd : CollapsedPoints)
				{
					const float Key = PointToAdd.GetKey(InputSpline);
					const FVector& Position = InputData.Spline->SplineStruct.GetTransform().InverseTransformPosition(PointToAdd.AveragePosition);

					// Find insertion index @todo_pcg: use bisection
					int InsertionIndex = SplinePoints.Num();
					for (int SPIndex = 0; SPIndex < SplinePoints.Num(); ++SPIndex)
					{
						if (SplinePoints[SPIndex].InputKey > Key)
						{
							InsertionIndex = SPIndex;
							break;
						}
					}

					bool bIsOnLastSegment = (InsertionIndex == SplinePoints.Num());

					// Skip points that are on control points - need to check against previous and next.
					FSplinePoint& PreviousPoint = SplinePoints[InsertionIndex - 1];
					FSplinePoint& NextPoint = SplinePoints[bIsOnLastSegment ? 0 : InsertionIndex];

					const float NextPointVirtualKey = (bIsOnLastSegment ? FMath::CeilToInt(PreviousPoint.InputKey + 1.0e-4f) : NextPoint.InputKey);

					if ((FMath::Abs(PreviousPoint.InputKey - Key) <= MinKeyDifferenceThreshold && (PreviousPoint.Position - Position).SquaredLength() <= SqrDistanceThreshold))
					{
						if (bNeedsMetadata)
						{
							// Mark previous point as an intersection
							OptionalWriteIntersectionMetadata(SplinePointsEntryKeys[InsertionIndex - 1], PointToAdd);
						}

						continue;
					}
					else if ((FMath::Abs(NextPointVirtualKey - Key) <= MinKeyDifferenceThreshold && (NextPoint.Position - Position).SquaredLength() <= SqrDistanceThreshold))
					{
						if (bNeedsMetadata)
						{
							// Mark next point as an intersection
							OptionalWriteIntersectionMetadata(SplinePointsEntryKeys[bIsOnLastSegment ? 0 : InsertionIndex], PointToAdd);
						}

						continue;
					}

					FSplinePoint SplinePoint;
					SplinePoint.InputKey = Key;
					SplinePoint.Position = Position;
					// @todo_pcg: we don't need to add points that are on the control point, however, we might want to do metadata for those
					SplinePoint.ArriveTangent = InputData.Spline->SplineStruct.GetTangentAtSplineInputKey(Key, ESplineCoordinateSpace::Local);
					SplinePoint.LeaveTangent = SplinePoint.ArriveTangent;
					SplinePoint.Rotation = InputData.Spline->SplineStruct.GetQuaternionAtSplineInputKey(Key, ESplineCoordinateSpace::Local).Rotator();
					SplinePoint.Scale = InputData.Spline->SplineStruct.GetScaleAtSplineInputKey(Key);
					SplinePoint.Type = ESplinePointType::CurveCustomTangent;

					// Adjust tangents
					const double KeyRange = FMath::Max(NextPointVirtualKey - PreviousPoint.InputKey, 1.0e-4);
					const double PreviousRatio = (Key - PreviousPoint.InputKey) / KeyRange;
					const double NextRatio = (NextPointVirtualKey - Key) / KeyRange;

					PreviousPoint.LeaveTangent *= PreviousRatio;
					NextPoint.ArriveTangent *= NextRatio;

					// The spline point tangent is "always" taken from the original spline's perspective,
					// so we will assume that the range is always one, so we need to scale accordingly.
					// @todo_pcg: get key at the beginning of the segment
					SplinePoint.ArriveTangent *= (Key - PreviousPoint.InputKey);
					SplinePoint.LeaveTangent *= (NextPointVirtualKey - Key);

					SplinePoints.Insert(SplinePoint, InsertionIndex);
					if (bHasMetadata || bNeedsMetadata)
					{
						PCGMetadataEntryKey EntryKey = PCGInvalidEntryKey;

						if (bHasMetadata)
						{
							InputData.Spline->WriteMetadataToEntry(Key, EntryKey, NewSplineData->MutableMetadata());
						}

						OptionalWriteIntersectionMetadata(EntryKey, PointToAdd);
						SplinePointsEntryKeys.Insert(EntryKey, InsertionIndex);
					}
				}

				// Mark all tangents as custom, and reset the keys
				for(int SPIndex = 0; SPIndex < SplinePoints.Num(); ++SPIndex)
				{
					FSplinePoint& SplinePoint = SplinePoints[SPIndex];
					SplinePoint.InputKey = static_cast<float>(SPIndex);
					if (SplinePoint.Type == ESplinePointType::Curve || SplinePoint.Type == ESplinePointType::CurveClamped)
					{
						SplinePoints[SPIndex].Type = ESplinePointType::CurveCustomTangent;
					}
				}

				// Initialize data now
				NewSplineData->Initialize(SplinePoints,
					InputData.Spline->SplineStruct.IsClosedLoop(),
					InputData.Spline->SplineStruct.GetTransform(),
					SplinePointsEntryKeys);

				Output.Data = NewSplineData;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
