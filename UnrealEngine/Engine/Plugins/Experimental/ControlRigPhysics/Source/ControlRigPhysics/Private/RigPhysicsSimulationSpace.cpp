// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigPhysicsSimulation.h"
#include "RigPhysicsSolverComponent.h"

#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"

#include "PhysicsControlHelpers.h"

#include "Engine/Engine.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsJointHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"

#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ShapeInstance.h"
#include "Chaos/Capsule.h"
#include "Chaos/PBDJointConstraintUtilities.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodySetup.h"

TAutoConsoleVariable<int> CVarControlRigPhysicsShowSimulationSpaceInfo(
	TEXT("ControlRig.Physics.ShowSimulationSpaceInfo"), 0,
	TEXT("Shows information associated with the simulation space used in control rig physics."));

//======================================================================================================================
FORCEINLINE_DEBUGGABLE FTransform GetSpaceTransform(
	ERigPhysicsSimulationSpace Space, const FTransform& ComponentTM, const FTransform& BoneTM)
{
	switch (Space)
	{
	case ERigPhysicsSimulationSpace::Component: return ComponentTM;
	case ERigPhysicsSimulationSpace::World: return FTransform::Identity;
	case ERigPhysicsSimulationSpace::SpaceBone: return BoneTM * ComponentTM;
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return FTransform::Identity;
	}
}

//======================================================================================================================
FTransform FRigPhysicsSimulation::GetSimulationSpaceTransform(const FRigPhysicsSolverSettings& SolverSettings) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return SimulationSpaceState.ComponentTM;
	case ERigPhysicsSimulationSpace::World: return FTransform::Identity;
	case ERigPhysicsSimulationSpace::SpaceBone: return SimulationSpaceState.BoneRelComponentTM * SimulationSpaceState.ComponentTM;
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return FTransform::Identity;
	}
}

//======================================================================================================================
FTransform FRigPhysicsSimulation::ConvertComponentSpaceTransformToSimSpace(
	const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return TM;
	case ERigPhysicsSimulationSpace::World:  return TM * SimulationSpaceState.ComponentTM;
	case ERigPhysicsSimulationSpace::SpaceBone: return TM.GetRelativeTransform(SimulationSpaceState.BoneRelComponentTM);
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return TM;
	}
}

//======================================================================================================================
FVector FRigPhysicsSimulation::ConvertComponentSpaceVectorToSimSpace(
	const FRigPhysicsSolverSettings& SolverSettings, const FVector& V) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return V;
	case ERigPhysicsSimulationSpace::World:  return SimulationSpaceState.ComponentTM.TransformVector(V);
	case ERigPhysicsSimulationSpace::SpaceBone: return SimulationSpaceState.BoneRelComponentTM.InverseTransformVector(V);
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return V;
	}
}

//======================================================================================================================
FTransform FRigPhysicsSimulation::ConvertSimSpaceTransformToComponentSpace(
	const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return TM;
	case ERigPhysicsSimulationSpace::World:  return TM.GetRelativeTransform(SimulationSpaceState.ComponentTM);
	case ERigPhysicsSimulationSpace::SpaceBone: return TM * SimulationSpaceState.BoneRelComponentTM;
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return TM;
	}
}

//======================================================================================================================
FORCEINLINE_DEBUGGABLE FVector ConvertWorldVectorToSimSpaceNoScale(
	ERigPhysicsSimulationSpace Space, const FVector& WorldVector, 
	const FTransform& ComponentTM, const FTransform& BoneTM)
{
	switch (Space)
	{
	case ERigPhysicsSimulationSpace::Component: return ComponentTM.InverseTransformVectorNoScale(WorldVector);
	case ERigPhysicsSimulationSpace::World: return WorldVector;
	case ERigPhysicsSimulationSpace::SpaceBone:
		return BoneTM.InverseTransformVectorNoScale(ComponentTM.InverseTransformVectorNoScale(WorldVector));
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return WorldVector;
	}
}

//======================================================================================================================
FVector FRigPhysicsSimulation::ConvertWorldVectorToSimSpaceNoScale(
	const FRigPhysicsSolverSettings& SolverSettings, const FVector& WorldVector) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return SimulationSpaceState.ComponentTM.InverseTransformVectorNoScale(WorldVector);
	case ERigPhysicsSimulationSpace::World: return WorldVector;
	case ERigPhysicsSimulationSpace::SpaceBone:
		return SimulationSpaceState.BoneRelComponentTM.InverseTransformVectorNoScale(
			SimulationSpaceState.ComponentTM.InverseTransformVectorNoScale(WorldVector));
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return WorldVector;
	}
}

//======================================================================================================================
FTransform FRigPhysicsSimulation::ConvertWorldTransformToSimSpace(
	const FRigPhysicsSolverSettings& SolverSettings, const FTransform& WorldTM) const
{
	switch (SolverSettings.SimulationSpace)
	{
	case ERigPhysicsSimulationSpace::Component: return WorldTM.GetRelativeTransform(SimulationSpaceState.ComponentTM);
	case ERigPhysicsSimulationSpace::World: return WorldTM;
	case ERigPhysicsSimulationSpace::SpaceBone:
		return WorldTM.GetRelativeTransform(SimulationSpaceState.BoneRelComponentTM * SimulationSpaceState.ComponentTM);
	default: ensureMsgf(false, TEXT("Unsupported Simulation Space")); return WorldTM;
	}
}

//======================================================================================================================
FTransform FRigPhysicsSimulation::ConvertCollisionSpaceTransformToSimSpace(
	const FRigPhysicsSolverSettings& SolverSettings, const FTransform& TM) const
{
	FTransform SimSpaceTM = GetSpaceTransform(
		SolverSettings.SimulationSpace, 
		SimulationSpaceState.ComponentTM, 
		SimulationSpaceState.BoneRelComponentTM);
	FTransform CollisionSpaceTM = GetSpaceTransform(
		SolverSettings.CollisionSpace, 
		SimulationSpaceState.ComponentTM, 
		SimulationSpaceState.BoneRelComponentTM);

	FTransform WorldSpaceTM = TM * CollisionSpaceTM;
	return WorldSpaceTM.GetRelativeTransform(SimSpaceTM);
}

//======================================================================================================================
void FRigPhysicsSimulation::InitSimulationSpace(
	const FTransform& ComponentTM,
	const FTransform& BoneRelComponentTM)
{
	SimulationSpaceState.ComponentTM = ComponentTM;
	SimulationSpaceState.BoneRelComponentTM = BoneRelComponentTM;
}


//======================================================================================================================
// Danny TODO We need support for double precision Perlin inputs - for now duplicate.
namespace FPerlinHelpers
{
	// random permutation of 256 numbers, repeated 2x
	static const int32 Permutation[512] = {
		63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99, 222, 2, 41, 15, 197, 93, 169, 90, 228, 43, 221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219,
		78, 224, 13, 193, 88, 134, 211, 7, 112, 176, 19, 106, 83, 75, 217, 85, 0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238, 172, 82, 110, 4, 227, 77, 243, 46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194,
		57, 48, 247, 233, 51, 105, 5, 23, 42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108, 241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21, 244, 60, 223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160,
		124, 156, 79, 186, 135, 127, 14, 121, 22, 65, 54, 153, 91, 213, 174, 24, 252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147, 25, 67, 36, 92, 236, 71, 166, 1, 187, 100, 130, 143, 237, 178, 158,
		104, 184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3, 248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74, 150, 218, 29, 133, 8, 44, 123, 28, 146, 89, 101, 154, 220, 126,
		155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164, 167, 215, 246, 144, 107, 235,

		63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99, 222, 2, 41, 15, 197, 93, 169, 90, 228, 43, 221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219,
		78, 224, 13, 193, 88, 134, 211, 7, 112, 176, 19, 106, 83, 75, 217, 85, 0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238, 172, 82, 110, 4, 227, 77, 243, 46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194,
		57, 48, 247, 233, 51, 105, 5, 23, 42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108, 241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21, 244, 60, 223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160,
		124, 156, 79, 186, 135, 127, 14, 121, 22, 65, 54, 153, 91, 213, 174, 24, 252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147, 25, 67, 36, 92, 236, 71, 166, 1, 187, 100, 130, 143, 237, 178, 158,
		104, 184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3, 248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74, 150, 218, 29, 133, 8, 44, 123, 28, 146, 89, 101, 154, 220, 126,
		155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164, 167, 215, 246, 144, 107, 235
	};

	// Gradient functions for 1D, 2D and 3D Perlin noise

	FORCEINLINE float Grad1(int32 Hash, double X)
	{
		// Slicing Perlin's 3D improved noise would give us only scales of -1, 0 and 1; this looks pretty bad so let's use a different sampling
		static const double Grad1Scales[16] = { -8 / 8, -7 / 8., -6 / 8., -5 / 8., -4 / 8., -3 / 8., -2 / 8., -1 / 8., 1 / 8., 2 / 8., 3 / 8., 4 / 8., 5 / 8., 6 / 8., 7 / 8., 8 / 8 };
		return Grad1Scales[Hash & 15] * X;
	}


	// Curve w/ second derivative vanishing at 0 and 1, from Perlin's improved noise paper
	FORCEINLINE double SmoothCurve(double X)
	{
		return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
	}

	FORCEINLINE float PerlinNoise1D(double X)
	{
		const double Xfl = FMath::FloorToFloat(X);
		const int64 Xi = (int64)(Xfl) & 255;
		X -= Xfl;
		const double Xm1 = X - 1.0;

		const int32 A = Permutation[Xi];
		const int32 B = Permutation[Xi + 1];

		const double U = SmoothCurve(X);

		// 2.0 factor to ensure (-1, 1) range
		return 2.0f * FMath::Lerp(Grad1(A, X), Grad1(B, Xm1), U);
	}
}



//======================================================================================================================
// Note - don't use the space conversion functions here as the state won't have been set yet.
FRigPhysicsSimulation::FSimulationSpaceData FRigPhysicsSimulation::UpdateSimulationSpaceStateAndCalculateData(
	const FRigVMExecuteContext&         ExecuteContext, 
	const URigHierarchy&                Hierarchy,
	const FRigPhysicsSolverComponent* SolverComponent,
	const float                         Dt,
	const double                        AbsoluteTime)
{
	const FRigPhysicsSolverSettings& SolverSettings = SolverComponent->SolverSettings;
	const FRigPhysicsSimulationSpaceSettings& SimulationSpaceSettings = SolverComponent->SimulationSpaceSettings;

	SimulationSpaceState.ComponentTM = ExecuteContext.GetToWorldSpaceTransform();

	if (SolverSettings.SimulationSpace == ERigPhysicsSimulationSpace::SpaceBone &&  SolverSettings.SpaceBone.IsValid())
	{
		SimulationSpaceState.BoneRelComponentTM = Hierarchy.GetGlobalTransform(SolverSettings.SpaceBone);
	}

	// Record the history - but avoid polluting it with zero Dt updates. What that means is - if we
	// get a zero-Dt update, then just update our current sim space TM, which means the time delta
	// from the previous state is actually the current Dt (i.e. don't overwrite the current Dt).
	if (Dt > SMALL_NUMBER)
	{
		SimulationSpaceState.PrevDt = SimulationSpaceState.Dt;
		SimulationSpaceState.Dt = Dt;

		SimulationSpaceState.PrevPrevSimulationSpaceTM = SimulationSpaceState.PrevSimulationSpaceTM;
		SimulationSpaceState.PrevSimulationSpaceTM = SimulationSpaceState.SimulationSpaceTM;
	}
	SimulationSpaceState.SimulationSpaceTM = GetSpaceTransform(
		SolverSettings.SimulationSpace, SimulationSpaceState.ComponentTM, SimulationSpaceState.BoneRelComponentTM);

	SimulationSpaceData = FSimulationSpaceData();
	SimulationSpaceData.Gravity = ::ConvertWorldVectorToSimSpaceNoScale(
		SolverSettings.SimulationSpace, SolverSettings.Gravity, 
		SimulationSpaceState.ComponentTM, SimulationSpaceState.BoneRelComponentTM);

	if (SolverSettings.SimulationSpace == ERigPhysicsSimulationSpace::World)
	{
		// Danny TODO This is probably redundant unless we support runtime switching of the space
		InitSimulationSpace(SimulationSpaceState.ComponentTM, SimulationSpaceState.BoneRelComponentTM);
		SimulationSpaceData.LinearVelocity = SimulationSpaceSettings.ExternalLinearVelocity;
		SimulationSpaceData.AngularVelocity = FMath::DegreesToRadians(SimulationSpaceSettings.ExternalAngularVelocity);
		return SimulationSpaceData;
	}

	// If the timestep is zero, then it doesn't actually matter what the velocity is - but make sure
	// it doesn't corrupt anything.
	if (SimulationSpaceState.Dt < SMALL_NUMBER)
	{
		SimulationSpaceData.LinearVelocity = FVector::ZeroVector;
		SimulationSpaceData.AngularVelocity = FVector::ZeroVector;
		SimulationSpaceData.LinearAcceleration = FVector::ZeroVector;
		SimulationSpaceData.AngularAcceleration = FVector::ZeroVector;
		return SimulationSpaceData;
	}

	// We calculate velocities etc in world space first, and then subsequently convert them into
	// simulation space.

	// Note that the velocity/accel calculations are intended to track the world/simulation behavior
	// - not necessarily be the most accurate calculations! For example, we could use one-sided
	// finite difference approximations, but this wouldn't necessarily be correct.

	// World-space component linear velocity and acceleration
	SimulationSpaceData.LinearVelocity = UE::PhysicsControl::CalculateLinearVelocity(
		SimulationSpaceState.PrevSimulationSpaceTM.GetTranslation(),
		SimulationSpaceState.SimulationSpaceTM.GetTranslation(), SimulationSpaceState.Dt);
	const FVector PrevSpaceLinearVel = 
		SimulationSpaceState.PrevDt < SMALL_NUMBER 
		? SimulationSpaceData.LinearVelocity 
		: UE::PhysicsControl::CalculateLinearVelocity(
			SimulationSpaceState.PrevPrevSimulationSpaceTM.GetTranslation(),
			SimulationSpaceState.PrevSimulationSpaceTM.GetTranslation(), SimulationSpaceState.PrevDt);
	SimulationSpaceData.LinearAcceleration =
		(SimulationSpaceData.LinearVelocity - PrevSpaceLinearVel) / SimulationSpaceState.Dt;

	// World-space component angular velocity and acceleration
	SimulationSpaceData.AngularVelocity = UE::PhysicsControl::CalculateAngularVelocity(
		SimulationSpaceState.PrevSimulationSpaceTM.GetRotation(),
		SimulationSpaceState.SimulationSpaceTM.GetRotation(), SimulationSpaceState.Dt);
	const FVector PrevSpaceAngularVel = 
		SimulationSpaceState.PrevDt < SMALL_NUMBER 
		? SimulationSpaceData.AngularVelocity
		: UE::PhysicsControl::CalculateAngularVelocity(
			SimulationSpaceState.PrevPrevSimulationSpaceTM.GetRotation(),
			SimulationSpaceState.PrevSimulationSpaceTM.GetRotation(), SimulationSpaceState.PrevDt);
	SimulationSpaceData.AngularAcceleration =
		(SimulationSpaceData.AngularVelocity - PrevSpaceAngularVel) / SimulationSpaceState.Dt;

	// Apply Z scale
	SimulationSpaceData.LinearVelocity.Z *= SimulationSpaceSettings.VelocityScaleZ;
	SimulationSpaceData.LinearAcceleration.Z *= SimulationSpaceSettings.VelocityScaleZ;

	bool bLinearAccelerationTrigger = SimulationSpaceSettings.LinearAccelerationThresholdForTeleport > 0 &&
		SimulationSpaceData.LinearAcceleration.SquaredLength() >
		FMath::Square(SimulationSpaceSettings.LinearAccelerationThresholdForTeleport);
	bool bAngularAccelerationTrigger = SimulationSpaceSettings.AngularAccelerationThresholdForTeleport > 0 &&
		SimulationSpaceData.AngularAcceleration.SquaredLength() >
		FMath::Square(FMath::DegreesToRadians(SimulationSpaceSettings.AngularAccelerationThresholdForTeleport));
	bool bPositionTrigger = SimulationSpaceSettings.PositionChangeThresholdForTeleport > 0 &&
		SimulationSpaceData.LinearVelocity.SquaredLength() >
		FMath::Square(SimulationSpaceSettings.PositionChangeThresholdForTeleport / SimulationSpaceState.Dt);
	bool bOrientationTrigger = SimulationSpaceSettings.OrientationChangeThresholdForTeleport > 0 &&
		SimulationSpaceData.AngularVelocity.SquaredLength() >
		FMath::Square(FMath::DegreesToRadians(
			SimulationSpaceSettings.OrientationChangeThresholdForTeleport / SimulationSpaceState.Dt));

	// Clamped world-space motion of the simulation space
	if (bLinearAccelerationTrigger || bAngularAccelerationTrigger || bPositionTrigger || bOrientationTrigger)
	{
		if (bLinearAccelerationTrigger)
		{
			UE_LOG(LogRigPhysics, Log, TEXT("Detected linear Acceleration (%f > %f) teleport in %s"),
				SimulationSpaceData.LinearAcceleration.Length(), 
				SimulationSpaceSettings.LinearAccelerationThresholdForTeleport,
				*OwnerName.ToString());
		}
		if (bAngularAccelerationTrigger)
		{
			UE_LOG(LogRigPhysics, Log, TEXT("Detected angular Acceleration (%f > %f) teleport in %s"),
				FMath::RadiansToDegrees(SimulationSpaceData.AngularAcceleration.Length()),
				SimulationSpaceSettings.AngularAccelerationThresholdForTeleport,
				*OwnerName.ToString());
		}
		if (bPositionTrigger)
		{
			UE_LOG(LogRigPhysics, Log, TEXT("Detected position (%f > %f) teleport in %s"),
				SimulationSpaceData.LinearVelocity.Length() * SimulationSpaceState.Dt,
				SimulationSpaceSettings.PositionChangeThresholdForTeleport,
				*OwnerName.ToString());
		}
		if (bOrientationTrigger)
		{
			UE_LOG(LogRigPhysics, Log, TEXT("Detected orientation (%f > %f) teleport in %s"),
				FMath::RadiansToDegrees(SimulationSpaceData.AngularVelocity.Length() * SimulationSpaceState.Dt),
				SimulationSpaceSettings.OrientationChangeThresholdForTeleport,
				*OwnerName.ToString());
		}

		// Note that a teleport detection shouldn't change the pose, or the current motion. We just
		// don't want to bring in that unwanted global motion.
		SimulationSpaceData.LinearVelocity = FVector::ZeroVector;
		SimulationSpaceData.AngularVelocity = FVector::ZeroVector;
		SimulationSpaceData.LinearAcceleration = FVector::ZeroVector;
		SimulationSpaceData.AngularAcceleration = FVector::ZeroVector;

		// This will stop the next step from using bogus values too.
		SimulationSpaceState.PrevSimulationSpaceTM = SimulationSpaceState.SimulationSpaceTM;
		SimulationSpaceState.PrevPrevSimulationSpaceTM = SimulationSpaceState.SimulationSpaceTM;
		SimulationSpaceState.PrevDt = 0;
		SimulationSpaceState.Dt = 0;
		// Avoid cached transforms being used in controls by bumping the update counter. 
		UpdateCounter += 1;
	}
	else
	{
		if (SimulationSpaceSettings.bClampLinearVelocity)
		{
			SimulationSpaceData.LinearVelocity =
				SimulationSpaceData.LinearVelocity.GetClampedToMaxSize(SimulationSpaceSettings.MaxLinearVelocity);
		}
		if (SimulationSpaceSettings.bClampAngularVelocity)
		{
			SimulationSpaceData.AngularVelocity =
				SimulationSpaceData.AngularVelocity.GetClampedToMaxSize(SimulationSpaceSettings.MaxAngularVelocity);
		}
		if (SimulationSpaceSettings.bClampLinearAcceleration)
		{
			SimulationSpaceData.LinearAcceleration =
				SimulationSpaceData.LinearAcceleration.GetClampedToMaxSize(SimulationSpaceSettings.MaxLinearAcceleration);
		}
		if (SimulationSpaceSettings.bClampAngularAcceleration)
		{
			SimulationSpaceData.AngularAcceleration =
				SimulationSpaceData.AngularAcceleration.GetClampedToMaxSize(SimulationSpaceSettings.MaxAngularAcceleration);
		}
	}

	SimulationSpaceData.LinearVelocity += SimulationSpaceSettings.ExternalLinearVelocity;
	SimulationSpaceData.AngularVelocity += FMath::DegreesToRadians(SimulationSpaceSettings.ExternalAngularVelocity);

	if (!SimulationSpaceSettings.ExternalTurbulenceVelocity.IsNearlyZero())
	{
		FVector T(
			FPerlinHelpers::PerlinNoise1D(AbsoluteTime),
			FPerlinHelpers::PerlinNoise1D(AbsoluteTime + 10.0),
			FPerlinHelpers::PerlinNoise1D(AbsoluteTime + 20.0));

		FVector Turbulence = T * SimulationSpaceSettings.ExternalTurbulenceVelocity;

		SimulationSpaceData.LinearVelocity += Turbulence;

		if (GEngine && CVarControlRigPhysicsShowSimulationSpaceInfo.GetValueOnAnyThread())
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 0.f, FColor::Yellow,
				FString::Printf(TEXT("Sim %s Turbulence %s"), *OwnerName.ToString(), *Turbulence.ToString()));
		}
	}

	// Transform world-space motion into simulation space Danny TODO note that this matches the code
	// in RBAN, and is doing what the interface requires (i.e. movement of the space in the space of
	// the space!). 
	SimulationSpaceData.LinearVelocity =
		SimulationSpaceState.SimulationSpaceTM.InverseTransformVector(SimulationSpaceData.LinearVelocity);
	SimulationSpaceData.AngularVelocity =
		SimulationSpaceState.SimulationSpaceTM.InverseTransformVector(SimulationSpaceData.AngularVelocity);
	SimulationSpaceData.LinearAcceleration =
		SimulationSpaceState.SimulationSpaceTM.InverseTransformVector(SimulationSpaceData.LinearAcceleration);
	SimulationSpaceData.AngularAcceleration =
		SimulationSpaceState.SimulationSpaceTM.InverseTransformVector(SimulationSpaceData.AngularAcceleration);

	if (GEngine && CVarControlRigPhysicsShowSimulationSpaceInfo.GetValueOnAnyThread())
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 0.f, FColor::Yellow,
			FString::Printf(TEXT("Sim %s dt %6.2fms Space P %35s V %35s A %35s"),
				*OwnerName.ToString(), Dt * 1000.0f,
				*SimulationSpaceState.SimulationSpaceTM.GetLocation().ToString(),
				*SimulationSpaceData.LinearVelocity.ToString(),
				*SimulationSpaceData.LinearAcceleration.ToString()));
	}

	return SimulationSpaceData;
}
