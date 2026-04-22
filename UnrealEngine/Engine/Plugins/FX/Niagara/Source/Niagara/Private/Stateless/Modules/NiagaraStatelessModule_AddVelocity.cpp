// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/NiagaraStatelessDrawDebugContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraStatelessModule_AddVelocity)

void UNiagaraStatelessModule_AddVelocity::BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
{
	if (!IsModuleEnabled())
	{
		return;
	}

	NiagaraStateless::FPhysicsBuildData& PhysicsBuildData = BuildContext.GetTransientBuildData<NiagaraStateless::FPhysicsBuildData>();
	if (VelocityType == ENSM_VelocityType::Linear)
	{
		const FNiagaraStatelessRangeVector3 VelocityRange = BuildContext.ConvertDistributionToRange(LinearVelocityDistribution, FVector3f::ZeroVector);
		PhysicsBuildData.VelocityCoordinateSpace = CoordinateSpace;
		PhysicsBuildData.VelocityRange = BuildContext.ConvertDistributionToRange(LinearVelocityDistribution, FVector3f::ZeroVector);
		PhysicsBuildData.LinearVelocityScale = BuildContext.ConvertDistributionToRange(LinearVelocityScale, 1.0f);
	}
	else if (VelocityType == ENSM_VelocityType::FromPoint)
	{
		ensureMsgf(PhysicsBuildData.bPointVelocity == false, TEXT("Only a single point force is supported at the moment."));

		PhysicsBuildData.bPointVelocity = true;
		PhysicsBuildData.PointCoordinateSpace = CoordinateSpace;
		PhysicsBuildData.PointVelocityRange = BuildContext.ConvertDistributionToRange(PointVelocityDistribution, 0.0f);
		PhysicsBuildData.PointOrigin = PointOrigin;
	}
	else if (VelocityType == ENSM_VelocityType::InCone)
	{
		ensureMsgf(PhysicsBuildData.bConeVelocity == false, TEXT("Only a single cone force is supported at the moment."));

		PhysicsBuildData.bConeVelocity = true;
		PhysicsBuildData.ConeCoordinateSpace = CoordinateSpace;
		PhysicsBuildData.ConeQuat = FQuat4f(ConeRotation.Quaternion());
		PhysicsBuildData.ConeVelocityRange = BuildContext.ConvertDistributionToRange(ConeVelocityDistribution, 0.0f);
		PhysicsBuildData.ConeOuterAngle = ConeAngle;
		PhysicsBuildData.ConeInnerAngle = InnerCone;
		PhysicsBuildData.ConeVelocityFalloff = bSpeedFalloffFromConeAxisEnabled ? FMath::Clamp(SpeedFalloffFromConeAxis, 0.0f, 1.0f) : 0.0f;
	}
}

#if WITH_EDITOR
void UNiagaraStatelessModule_AddVelocity::DrawDebug(const FNiagaraStatelessDrawDebugContext& DrawDebugContext) const
{
	const FVector WorldOrigin = DrawDebugContext.TransformPosition(FVector3f::ZeroVector);

	switch (VelocityType)
	{
		case ENSM_VelocityType::Linear:
		{
			const FNiagaraStatelessRangeVector3 VelocityRange = LinearVelocityDistribution.CalculateRange(FVector3f::ZeroVector);
			const FNiagaraStatelessRangeFloat LinearVelocityScaleRange = LinearVelocityScale.CalculateRange();
			const FVector MinDir = FVector(VelocityRange.Min * LinearVelocityScale.Min);
			const FVector MaxDir = FVector(VelocityRange.Max * LinearVelocityScale.Max);
			DrawDebugContext.DrawArrow(FVector::ZeroVector, MinDir);

			if (!FMath::IsNearlyEqual(MinDir.Length(), MaxDir.Length()))
			{
				DrawDebugContext.DrawArrow(WorldOrigin, MaxDir);
			}
			break;
		}

		case ENSM_VelocityType::InCone:
		{
			const FQuat ConeQuat = DrawDebugContext.TransformRotation(FQuat4f(ConeRotation.Quaternion()));
			const float ConeHAngle = ConeAngle / 2.0f;

			TOptional<float> InnerConeHAngle;
			if (InnerCone > 0.0f && !FMath::IsNearlyEqual(ConeAngle, InnerCone))
			{
				InnerConeHAngle = InnerCone / 2.0f;
			}

			const FNiagaraStatelessRangeFloat ConeVelocityRange = ConeVelocityDistribution.CalculateRange(0.0f);
			DrawDebugContext.DrawCone(WorldOrigin, ConeQuat, ConeHAngle, ConeVelocityRange.Min);
			if ( InnerConeHAngle.IsSet() )
			{
				DrawDebugContext.DrawCone(WorldOrigin, ConeQuat, InnerConeHAngle.GetValue(), ConeVelocityRange.Min);
			}

			if (!FMath::IsNearlyEqual(ConeVelocityRange.Min, ConeVelocityRange.Max))
			{
				DrawDebugContext.DrawCone(WorldOrigin, ConeQuat, ConeHAngle, ConeVelocityRange.Max);
				if (InnerConeHAngle.IsSet())
				{
					DrawDebugContext.DrawCone(WorldOrigin, ConeQuat, InnerConeHAngle.GetValue(), ConeVelocityRange.Max);
				}
			}
			break;
		}

		case ENSM_VelocityType::FromPoint:
		{
			const FNiagaraStatelessRangeFloat PointVelocityRange = PointVelocityDistribution.CalculateRange(0.0f);
			if (!FMath::IsNearlyEqual(PointVelocityRange.Min, 0.0f))
			{
				DrawDebugContext.DrawSphere(DrawDebugContext.TransformPosition(PointOrigin), PointVelocityRange.Min);
			}
			if (!FMath::IsNearlyEqual(PointVelocityRange.Min, PointVelocityRange.Max))
			{
				DrawDebugContext.DrawSphere(DrawDebugContext.TransformPosition(PointOrigin), PointVelocityRange.Max);
			}
			break;
		}
	}
}
#endif
