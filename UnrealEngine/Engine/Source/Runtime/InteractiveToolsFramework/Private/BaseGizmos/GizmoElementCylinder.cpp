// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementCylinder.h"

#include "Algo/ForEach.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "DynamicMeshBuilder.h"
#include "GizmoPrivateUtil.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "SphereTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GizmoElementCylinder)

namespace GizmoElementCylinderLocals
{
	/**
	 * Normalizes the given dash parameters to fit the input range.
	 * @param	OutScaleFactor the scale factor to apply to the dash length and gap length.
	 * @return	the number of dashes that fit within the range.
	 */
	template < typename RealType = float>
	int32 GetDashNumAndScale(const RealType& InLineLength, const RealType& InDashLength, const RealType& InDashGapLength, RealType& OutScaleFactor)
	{
		const RealType DashLengthAndGap = InDashLength + InDashGapLength;
		OutScaleFactor = static_cast<RealType>(1);

		// Number based on sequence of dash, gap pairs, and last dash. This should always be an even number for fit alignment
		const int32 NumDashes = FMath::Max(2, FMath::CeilToInt32((InLineLength / DashLengthAndGap)));

		// Get a scale factor based on how many dashes with gaps (and an end dash) can actually fit on this line
		OutScaleFactor = InLineLength / (((NumDashes - 1) * DashLengthAndGap) + InDashLength);

		return NumDashes;
	}
}

void UGizmoElementCylinder::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, Base, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		if (const UMaterialInterface* UseMaterial = CurrentRenderState.GetCurrentMaterial())
		{
			const FQuat Rotation = FRotationMatrix::MakeFromZ(Direction).ToQuat();
			const double HalfHeight = Height * 0.5;
			const FVector OriginOffset = Direction * HalfHeight;

			FTransform RenderLocalToWorldTransform = FTransform(Rotation, OriginOffset) * CurrentRenderState.LocalToWorldTransform;
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

			if (bIsDashed)
			{
				// Get a scale factor based on how many dashes with gaps (and an end dash) can actually fit on this line
				float DashLengthScale = 0.0f;
				
				constexpr int32 MaxNumDashes = 512; // Arbitrary maximum to prevent overflow in the case of very long cylinders

				// Number based on sequence of dash, gap pairs, and last dash
				const int32 NumDashes = FMath::Min(MaxNumDashes, GizmoElementCylinderLocals::GetDashNumAndScale(FMath::Max(0.0f, Height - DashLength), DashLength, DashGapLength, DashLengthScale));

				const float AdjustedDashLength = DashLength * DashLengthScale;
				const float AdjustDashGapLength = DashGapLength * DashLengthScale;

				const float DashLengthAndGaps = (AdjustedDashLength + AdjustDashGapLength);

				const float HalfDashLength = AdjustedDashLength * 0.5f;

				for (int32 DashIndex = 0; DashIndex <= NumDashes; ++DashIndex)
				{
					DrawCylinder(
						PDI,
						RenderLocalToWorldTransform.ToMatrixWithScale(),
						(-FVector::UpVector * HalfHeight) + FVector::UpVector * (HalfDashLength + (DashIndex * DashLengthAndGaps)),
						FVector(1, 0, 0),
						FVector(0, 1, 0),
						FVector(0, 0, 1),
						Radius,
						HalfDashLength,
						NumSides,
						UseMaterial->GetRenderProxy(),
						SDPG_Foreground);
				}
			}
			else
			{
				DrawCylinder(
					PDI,
					RenderLocalToWorldTransform.ToMatrixWithScale(),
					FVector::ZeroVector,
					FVector(1, 0, 0),
					FVector(0, 1, 0),
					FVector(0, 0, 1),
					Radius,
					HalfHeight,
					NumSides,
					UseMaterial->GetRenderProxy(),
					SDPG_Foreground);
			}
		}
	}
}

FInputRayHit UGizmoElementCylinder::LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection, FLineTraceOutput& OutLineTraceOutput)
{
	FLineTraceTraversalState CurrentLineTraceState(LineTraceState);
	bool bHittableViewDependent = UpdateLineTraceState(ViewContext, Base, CurrentLineTraceState);

	if (bHittableViewDependent)
	{
		bool bIntersects = false;
		double RayParam = 0.0;
		
		double PixelHitThresholdAdjust = CurrentLineTraceState.PixelToWorldScale * PixelHitDistanceThreshold;
		double WorldHeight = Height * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust * 2.0;
		double WorldRadius = Radius * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust;
		const FVector WorldDirection = CurrentLineTraceState.LocalToWorldTransform.TransformVectorNoScale(Direction);
		const FVector LocalCenter = Direction * Height * 0.5;
		const FVector WorldCenter = CurrentLineTraceState.LocalToWorldTransform.TransformPosition(LocalCenter);

		// due to numerical imprecision, the ray origin needs to be clamped in ortho views
		// (cf. UEditorInteractiveToolsContext::GetRayFromMousePos)
		FVector ClampedRayOrigin(RayOrigin);
		const double DepthBias = UE::GizmoUtil::ClampRayOrigin(ViewContext, ClampedRayOrigin, RayDirection);
		
		GizmoMath::RayCylinderIntersection(
			WorldCenter,
			WorldDirection,
			WorldRadius,
			WorldHeight,
			ClampedRayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			// add the depth bias if any
			RayParam += DepthBias;

			FInputRayHit RayHit = MakeRayHit(RayParam, OutLineTraceOutput);

			UE::Geometry::TRay<double> Ray(ClampedRayOrigin, RayDirection);
			FVector HitPoint = Ray.PointAt(RayParam);

			// We successfully hit, now check if we intersect the actual surface (rather than just within the PixelHitDistanceThreshold) ...
			// ... by negating the threshold
			WorldHeight -= PixelHitThresholdAdjust * 2.0;
			WorldRadius -= PixelHitThresholdAdjust;

			const double MinimumPixelHitThresholdAdjust = CurrentLineTraceState.PixelToWorldScale * MinimumPixelHitDistanceThreshold;
			WorldHeight += MinimumPixelHitThresholdAdjust * 2.0;
			WorldRadius += MinimumPixelHitThresholdAdjust;

			// Clamp both height and radius to a minimum of PixelToWorldScale to ensure hittability
			WorldHeight = FMath::Max(CurrentLineTraceState.PixelToWorldScale, WorldHeight);
			WorldRadius = FMath::Max(CurrentLineTraceState.PixelToWorldScale, WorldRadius);

			GizmoMath::RayCylinderIntersection(
				WorldCenter,
				WorldDirection,
				WorldRadius,
				WorldHeight,
				ClampedRayOrigin, RayDirection,
				bIntersects, RayParam);

			// We hit the surface
			if (bIntersects)
			{
				RayHit.HitDepth = RayParam + DepthBias;
				OutLineTraceOutput.bIsSurfaceHit = true;
			}

			return RayHit;
		}
	}

	return FInputRayHit();
}

void UGizmoElementCylinder::SetBase(const FVector& InBase)
{
	Base = InBase;
}

FVector UGizmoElementCylinder::GetBase() const
{
	return Base;
}

void UGizmoElementCylinder::SetDirection(const FVector& InDirection)
{
	Direction = InDirection;
	Direction.Normalize();
}

FVector UGizmoElementCylinder::GetDirection() const
{
	return Direction;
}

void UGizmoElementCylinder::SetHeight(float InHeight)
{
	Height = InHeight;
}

float UGizmoElementCylinder::GetHeight() const
{
	return Height;
}

void UGizmoElementCylinder::SetRadius(float InRadius)
{
	Radius = InRadius;
}

float UGizmoElementCylinder::GetRadius() const
{
	return Radius;
}

void UGizmoElementCylinder::SetNumSides(int32 InNumSides)
{
	NumSides = InNumSides;
}

int32 UGizmoElementCylinder::GetNumSides() const
{
	return NumSides;
}

void UGizmoElementCylinder::SetIsDashed(bool bInDashing)
{
	bIsDashed = bInDashing;
}

bool UGizmoElementCylinder::GetIsDashed() const
{
	return bIsDashed;
}

void UGizmoElementCylinder::SetDashParameters(const float InDashLength, const TOptional<float>& InGapLength)
{
	DashLength = InDashLength;
	DashGapLength = InGapLength.Get(InDashLength * 0.5f);
}

void UGizmoElementCylinder::GetDashParameters(float& OutDashLength, float& OutGapLength) const
{
	OutDashLength = DashLength;
	OutGapLength = DashGapLength;
}

