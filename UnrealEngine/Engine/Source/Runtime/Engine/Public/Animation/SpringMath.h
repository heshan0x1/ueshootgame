// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

// Collection of useful spring methods which can be used for damping, simulating characters etc.
// Reference https://theorangeduck.com/page/spring-roll-call

struct SpringMath
{
private:
	static constexpr float SmoothingTimeToDamping(float SmoothingTime)
	{
		return 4.0f / FMath::Max(SmoothingTime, UE_KINDA_SMALL_NUMBER);
	}
	
public:
	/** Convert a smoothing time to a half life
	 * 
	 * @param SmoothingTime The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate. 
	 * @return The half life of the spring. How long it takes the value to get halfway towards the target. 
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr float SmoothingTimeToHalfLife(float SmoothingTime)
	{
		return SmoothingTime * UE_LN2;
	}

	/** Convert a halflife to a smoothing time
	 * 
	 * @param HalfLife The half life of the spring. How long it takes the value to get halfway towards the target.
	 * @return The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr float HalfLifeToSmoothingTime(float HalfLife)
	{
		return HalfLife / UE_LN2;
	}

	/** Convert from smoothing time to spring strength.
	  * 
	 * @param SmoothingTime The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
	 * @return The spring strength. This corresponds to the undamped frequency of the spring in hz.
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr float SmoothingTimeToStrength(float SmoothingTime)
	{
		return 2.0f / FMath::Max(SmoothingTime, UE_KINDA_SMALL_NUMBER);
	}

	/** Convert from spring strength to smoothing time.
	 * 
	 * @param Strength The spring strength. This corresponds to the undamped frequency of the spring in hz.
	 * @return The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr float StrengthToSmoothingTime(float Strength)
	{
		return 2.0f / FMath::Max(Strength, UE_KINDA_SMALL_NUMBER);
	}

	/** Simplified version of FMath::CriticallyDampedSmoothing where v_goal is assumed to be 0. This interpolates the value InOutX towards TargetX
	 * with the motion of a critically damped spring. The velocity of InOutX is stored in InOutV.
	 * 
	 * @tparam T The type to be damped 
	 * @param InOutX The value to be damped
	 * @param InOutV The speed of the value to be damped
	 * @param TargetX The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutX to TargetX
	 * @param DeltaTime Timestep in seconds
	 */
	template <typename T>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void CriticalSpringDamper(
		T& InOutX,
		T& InOutV,
		T TargetX,
		float SmoothingTime,
		float DeltaTime)
	{
		float Y = SmoothingTimeToDamping(SmoothingTime) / 2.0f;
		T J0 = InOutX - TargetX;
		T J1 = InOutV + J0 * Y;
		float EyDt = FMath::InvExpApprox(Y * DeltaTime);

		InOutX = EyDt * (J0 + J1 * DeltaTime) + TargetX;
		InOutV = EyDt * (InOutV - J1 * Y * DeltaTime);
	}

	/** Specialized angle version of CriticalSpringDamper that handles angle wrapping.
	 *
	 * @param InOutAngleRadians The value to be damped
	 * @param InOutAngularVelocityRadians The speed of the value to be damped
	 * @param TargetAngleRadians The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutAngleRadians to TargetAngleRadians
	 * @param DeltaTime Timestep in seconds
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void CriticalSpringDamperAngle(
		float& InOutAngleRadians,
		float& InOutAngularVelocityRadians,
		float TargetAngleRadians,
		float SmoothingTime,
		float DeltaTime)
	{
		PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
		float Y = SmoothingTimeToDamping(SmoothingTime) / 2.0f;
		PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

		float J0 = FMath::FindDeltaAngleRadians(TargetAngleRadians, InOutAngleRadians);
		float J1 = InOutAngularVelocityRadians + J0 * Y;
		float EyDt = FMath::InvExpApprox(Y * DeltaTime);

		InOutAngleRadians = EyDt * (J0 + J1 * DeltaTime) + TargetAngleRadians;
		InOutAngularVelocityRadians = EyDt * (InOutAngularVelocityRadians - J1 * Y * DeltaTime);
	}

	/** Specialized quaternion version of CriticalSpringDamper, uses FVector for angular velocity
	 *
	 * @param InOutRotation The value to be damped
	 * @param InOutAngularVelocityRadians The angular velocity of the rotation in radians
	 * @param TargetRotation The target rotation to damp towards
	 * @param SmoothingTime he smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutRotation to TargetRotation
	 * @param DeltaTime
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void CriticalSpringDamperQuat(
		FQuat& InOutRotation,
		FVector& InOutAngularVelocityRadians,
		const FQuat& TargetRotation,
		float SmoothingTime,
		float DeltaTime)
	{
		PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
		float Y = SmoothingTimeToDamping(SmoothingTime) / 2.0f;
		PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

		FQuat Diff = InOutRotation * TargetRotation.Inverse();
		Diff.EnforceShortestArcWith(FQuat::Identity);
		FVector J0 = Diff.ToRotationVector();
		FVector J1 = InOutAngularVelocityRadians + J0 * Y;

		float EyDt = FMath::InvExpApprox(Y * DeltaTime);

		
		InOutRotation = FQuat::MakeFromRotationVector(EyDt * (J0 + J1 * DeltaTime)) * TargetRotation;
		InOutAngularVelocityRadians = EyDt * (InOutAngularVelocityRadians - J1 * Y * DeltaTime);
	}

	/** A velocity spring will damp towards a target that follows a fixed linear target velocity, allowing control of the interpolation speed
	 * while still giving a smoothed behavior. A SmoothingTime of 0 will give a linear interpolation between X and TargetX
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The velocity of the value to be damped
	 * @param InOutXi The intermediate target of the value to be damped
	 * @param TargetX The target value of X to damp towards
	 * @param MaxSpeed The desired speed to achieve while damping towards X
	 * @param SmoothingTime The smoothing time to use while damping towards X. Higher values will give more smoothed behaviour. A value of 0 will give a linear interpolation of X to Target
	 * @param DeltaTime The timestep in seconds
	 */
	template <typename TFloat>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void VelocitySpringDamperF(
		TFloat& InOutX,
		TFloat& InOutV,
		TFloat& InOutXi,
		TFloat TargetX,
		TFloat MaxSpeed,
		float SmoothingTime,
		float DeltaTime)
	{
		static_assert(std::is_floating_point_v<TFloat>, "TFloat must be floating point");

		MaxSpeed = FMath::Max(MaxSpeed, 0.0f); // MaxSpeed can't be negative

		TFloat XDiff = ((TargetX - InOutXi) > 0.0f
			? 1.0f
			: -1.0f) * MaxSpeed;

		float TGoalFuture = SmoothingTime;
		TFloat XGoalFuture = fabs(TargetX - InOutXi) > TGoalFuture * MaxSpeed
			? InOutXi + XDiff * TGoalFuture
			: TargetX;

		CriticalSpringDamper(InOutX, InOutV, XGoalFuture, SmoothingTime, DeltaTime);

		InOutXi = FMath::Abs(TargetX - InOutXi) > DeltaTime * MaxSpeed
			? InOutXi + XDiff * DeltaTime
			: TargetX;
	}

	/** A velocity spring will damp towards a target that follows a fixed linear target velocity, allowing control of the interpolation speed
	* while still giving a smoothed behaviour. A SmoothingTime of 0 will give a linear interpolation between X and TargetX
	 * 
	 * @tparam TVector The type of vector to use (e.g. FVector2D, FVector4, FVector etc)
	 * @param InOutX The value to be damped
	 * @param InOutV The velocity of the value to be damped
	 * @param InOutXi The intermediate target of the value to be damped
	 * @param TargetX The target value of X to damp towards
	 * @param MaxSpeed The max velocity to use for the intermediate target interpolation
	 * @param SmoothingTime The smoothing time to use while damping towards X. Higher values will give more smoothed behaviour. A value of 0 will give a linear interpolation of X to Target
	 * @param DeltaTime The timestep in seconds
	 */
	template <typename TVector>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void VelocitySpringDamper(
		TVector& InOutX,
		TVector& InOutV,
		TVector& InOutXi,
		TVector TargetX,
		float MaxSpeed,
		float SmoothingTime,
		float DeltaTime)
	{
		TVector XDiff = TargetX - InOutXi;
		float XDiffLength = XDiff.Length();
		TVector XDiffDir = XDiffLength > FLT_EPSILON
			? XDiff / XDiffLength
			: TVector::ZeroVector;

		float TGoalFuture = SmoothingTime;
		TVector XGoalFuture = XDiffLength > TGoalFuture * MaxSpeed
			? InOutXi + (XDiffDir * MaxSpeed) * TGoalFuture
			: TargetX;

		CriticalSpringDamper(InOutX, InOutV, XGoalFuture, SmoothingTime, DeltaTime);

		InOutXi = XDiffLength > DeltaTime * MaxSpeed
			? InOutXi + XDiffDir * MaxSpeed * DeltaTime
			: TargetX;
	}

	/** Update the position of a character given a target velocity using a simple damped spring
	 * 
	 * @tparam TVector The type of vector to use (e.g. FVector2D, FVector4, FVector etc)
	 * @param InOutPosition The position of the character
	 * @param InOutVelocity The velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutAcceleration The acceleration of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param TargetVelocity The target velocity of the character.
	 * @param SmoothingTime The time over which to smooth velocity. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 * @param DeltaTime The delta time to tick the character
	 * @param VDeadzone Deadzone for velocity. Current velocity will snap to target velocity when within the deadzone
	 * @param ADeadzone Deadzone for acceleration. Acceleration will snap to zero when within the deadzone
	 */
	template <typename TVector>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void SpringCharacterUpdate(
		TVector& InOutPosition,
		TVector& InOutVelocity,
		TVector& InOutAcceleration,
		const TVector& TargetVelocity,
		float SmoothingTime,
		float DeltaTime,
		float VDeadzone = 1e-2f,
		float ADeadzone = 1e-4f)
	{
		float Y = SmoothingTimeToDamping(SmoothingTime) / 2.0f;
		TVector J0 = InOutVelocity - TargetVelocity;
		TVector J1 = InOutAcceleration + J0 * Y;
		float EyDt = FMath::InvExpApprox(Y * DeltaTime);

		InOutPosition = EyDt * (((-J1) / (Y * Y)) + ((-J0 - J1 * DeltaTime) / Y)) +
			(J1 / (Y * Y)) + J0 / Y + TargetVelocity * DeltaTime + InOutPosition;
		InOutVelocity = EyDt * (J0 + J1 * DeltaTime) + TargetVelocity;
		InOutAcceleration = EyDt * (InOutAcceleration - J1 * Y * DeltaTime);

		if ((TargetVelocity - InOutVelocity).SquaredLength() < FMath::Square(VDeadzone))
		{
			// We reached our target
			InOutVelocity = TargetVelocity;

			if (InOutAcceleration.SquaredLength() < FMath::Square(ADeadzone))
			{
				InOutAcceleration = TVector::ZeroVector;
			}
		}
	}

	/** Gives predicted positions, velocities and accelerations for SpringCharacterUpdate. Useful for generating a predicted trajectory given known initial start conditions.
	 *
	 * @tparam TVector The type of vector to use (e.g. FVector2D, FVector4, FVector etc)
	 * @param OutPredictedPositions ArrayView of output buffer to put the predicted positions. ArrayView should be the same size as PredictCount
	 * @param OutPredictedVelocities ArrayView of output buffer to put the predicted velocities. ArrayView should be the same size as PredictCount
	 * @param OutPredictedAccelerations ArrayView of output buffer to put the predicted accelerations. ArrayView should be the same size as PredictCount
	 * @param PredictCount How many points to predict. Must be greater than 0
	 * @param CurrentPosition The initial position of the character
	 * @param CurrentVelocity The initial velocity of the character
	 * @param CurrentAcceleration The initial acceleration of the character
	 * @param TargetVelocity The target velocity of the character
	 * @param SmoothingTime The smoothing time of the character. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 * @param SecondsPerPredictionStep How much time in between each prediction step.
	 * @param VDeadzone Deadzone for velocity. Current velocity will snap to target velocity when within the deadzone
	 * @param ADeadzone Deadzone for acceleration. Acceleration will snap to zero when within the deadzone
	 */
	template <typename TVector>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void SpringCharacterPredict(
		TArrayView<TVector> OutPredictedPositions,
		TArrayView<TVector> OutPredictedVelocities,
		TArrayView<TVector> OutPredictedAccelerations,
		const TVector& CurrentPosition,
		const TVector& CurrentVelocity,
		const TVector& CurrentAcceleration,
		const TVector& TargetVelocity,
		float SmoothingTime,
		float SecondsPerPredictionStep,
		float VDeadzone = 1e-2f,
		float ADeadzone = 1e-4f)
	{
		int32 PredictCount = OutPredictedPositions.Num();
		check(PredictCount > 0);
		check(OutPredictedVelocities.Num() == PredictCount);
		check(OutPredictedAccelerations.Num() == PredictCount);
		
		for (int32 i = 0; i < PredictCount; i++)
		{
			OutPredictedPositions[i] = CurrentPosition;
			OutPredictedVelocities[i] = CurrentVelocity;
			OutPredictedAccelerations[i] = CurrentAcceleration;
		}

		for (int32 i = 0; i < PredictCount; i++)
		{
			const float PredictTime = (float)(i + 1) * SecondsPerPredictionStep; // Note i+1 since we want index 0 to be the first prediction step
			PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
			SpringCharacterUpdate(OutPredictedPositions[i], OutPredictedVelocities[i], OutPredictedAccelerations[i], TargetVelocity, SmoothingTime,
				PredictTime, VDeadzone, ADeadzone);
			PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
		}
	}

	/** Update a position representing a character given a target velocity using a velocity spring.
	 * A velocity spring tracks an intermediate velocity which moves at a maximum acceleration linearly towards a target.
	 * This means unlike the "SpringCharacterUpdate", it will take longer to reach a target velocity that is further away from the current velocity.
	 * 
	 * @tparam TVector The type of vector to use (e.g. FVector2D, FVector4, FVector etc)
	 * @param InOutPosition The position of the character
	 * @param InOutVelocity The velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutVelocityIntermediate The intermediate velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutAcceleration The acceleration of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param TargetVelocity The target velocity of the character.
	 * @param SmoothingTime The time over which to smooth velocity. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 * @param MaxAcceleration Puts a limit on the maximum acceleration that the intermediate velocity can do each frame. If MaxAccel is very large, the behaviour wil lbe the same as SpringCharacterUpdate
	 * @param DeltaTime The delta time to tick the character
	 * @param VDeadzone Deadzone for velocity. Current velocity will snap to target velocity when within the deadzone
	 * @param ADeadzone Deadzone for acceleration. Acceleration will snap to zero when within the deadzone
	 */
	template <typename TVector>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void VelocitySpringCharacterUpdate(
		TVector& InOutPosition,
		TVector& InOutVelocity,
		TVector& InOutVelocityIntermediate,
		TVector& InOutAcceleration,
		TVector TargetVelocity,
		float SmoothingTime,
		float MaxAcceleration,
		float DeltaTime,
		float VDeadzone = 1e-2f,
		float ADeadzone = 1e-4f)
	{
		TVector VDiff = TargetVelocity - InOutVelocityIntermediate;
		float VDiffLength = VDiff.Length();
		TVector VDiffDir = VDiffLength > 0.0001f
			? VDiff / VDiffLength
			: TVector::ZeroVector;

		float TGoalFuture = SmoothingTime;
		TVector MaxVFuture = VDiffLength > TGoalFuture * MaxAcceleration
			? InOutVelocityIntermediate + (VDiffDir * MaxAcceleration) * TGoalFuture
			: TargetVelocity;

		float Y = SmoothingTimeToDamping(SmoothingTime) / 2.0f;
		TVector J0 = InOutVelocity - MaxVFuture;
		TVector J1 = InOutAcceleration + J0 * Y;
		float EyDt = FMath::InvExpApprox(Y * DeltaTime);

		InOutPosition = EyDt * (((-J1) / (Y * Y)) + ((-J0 - J1 * DeltaTime) / Y)) +
			(J1 / (Y * Y)) + J0 / Y + MaxVFuture * DeltaTime + InOutPosition;
		InOutVelocity = EyDt * (J0 + J1 * DeltaTime) + MaxVFuture;
		InOutAcceleration = EyDt * (InOutAcceleration - J1 * Y * DeltaTime);
		InOutVelocityIntermediate = VDiffLength > DeltaTime * MaxAcceleration
			? InOutVelocityIntermediate + VDiffDir * MaxAcceleration * DeltaTime
			: TargetVelocity;

		if ((TargetVelocity - InOutVelocity).SquaredLength() < FMath::Square(VDeadzone))
		{
			// We reached our target
			InOutVelocity = TargetVelocity;

			if (InOutAcceleration.SquaredLength() < FMath::Square(ADeadzone))
			{
				InOutAcceleration = TVector::ZeroVector;
			}
		}
	}

	/** Gives predicted positions, velocities and accelerations for SpringCharacterUpdate. Useful for generating a predicted trajectory given known initial start conditions.
	 *
	 * @tparam TVector The type of vector to use (e.g. FVector2D, FVector4, FVector etc)
	 * @param OutPredictedPositions ArrayView of output buffer to put the predicted positions. ArrayView should be the same size as PredictCount
	 * @param OutPredictedVelocities ArrayView of output buffer to put the predicted velocities. ArrayView should be the same size as PredictCount
	 * @param OutPredictedIntermediateVelocities ArrayView of output buffer to put the predicted intermediate velocities. ArrayView should be the same size as PredictCount
	 * @param OutPredictedAccelerations ArrayView of output buffer to put the predicted accelerations. ArrayView should be the same size as PredictCount
	 * @param PredictCount How many points to predict. Must be greater than 0
	 * @param CurrentPosition The initial position of the character
	 * @param CurrentVelocity The initial velocity of the character
	 * @param CurrentIntermediateVelocity The initial intermediate velocity of the character
	 * @param CurrentAcceleration The initial acceleration of the character
	 * @param TargetVelocity The target velocity of the character
	 * @param SmoothingTime The smoothing time of the character. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 * @param MaxAcceleration Puts a limit on the maximum acceleration that the intermediate velocity can do each frame. If MaxAccel is very large, the behaviour wil lbe the same as SpringCharacterUpdate
	 * @param SecondsPerPredictionStep How much time in between each prediction step.
	 * @param VDeadzone Deadzone for velocity. Current velocity will snap to target velocity when within the deadzone
	 * @param ADeadzone Deadzone for acceleration. Acceleration will snap to zero when within the deadzone
	 */
	template <typename TVector>
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void VelocitySpringCharacterPredict(
		TArrayView<TVector> OutPredictedPositions,
		TArrayView<TVector> OutPredictedVelocities,
		TArrayView<TVector> OutPredictedIntermediateVelocities,
		TArrayView<TVector> OutPredictedAccelerations,
		const TVector& CurrentPosition,
		const TVector& CurrentVelocity,
		const TVector& CurrentIntermediateVelocity,
		const TVector& CurrentAcceleration,
		const TVector& TargetVelocity,
		float SmoothingTime,
		float MaxAcceleration,
		float SecondsPerPredictionStep,
		float VDeadzone = 1e-2f,
		float ADeadzone = 1e-4f)
	{
		int32 PredictCount = OutPredictedPositions.Num();
		check(PredictCount > 0);
		check(OutPredictedVelocities.Num() == PredictCount);
		check(OutPredictedAccelerations.Num() == PredictCount);
		check(OutPredictedIntermediateVelocities.Num() == PredictCount);
		
		for (int32 i = 0; i < PredictCount; i++)
		{
			OutPredictedPositions[i] = CurrentPosition;
			OutPredictedVelocities[i] = CurrentVelocity;
			OutPredictedIntermediateVelocities[i] = CurrentIntermediateVelocity;
			OutPredictedAccelerations[i] = CurrentAcceleration;
		}

		for (int32 i = 0; i < PredictCount; i++)
		{
			const float PredictTime = (float)(i + 1) * SecondsPerPredictionStep; // Note i+1 since we want index 0 to be the first prediction step
			VelocitySpringCharacterUpdate(OutPredictedPositions[i],
				OutPredictedVelocities[i],
				OutPredictedIntermediateVelocities[i],
				OutPredictedAccelerations[i],
				TargetVelocity,
				SmoothingTime,
				MaxAcceleration,
				PredictTime,
				VDeadzone,
				ADeadzone);
		}
	}

	/** Prediction of CriticalSpringDamperQuat
	 * 
	 * @param OutPredictedRotations ArrayView of output buffer to put the predicted rotations. ArrayView should be the same size as PredictCount
	 * @param OutPredictedAngularVelocities ArrayView of output buffer to put the predicted angular velocities. ArrayView should be the same size as PredictCount
	 * @param PredictCount How many points to predict. Must be greater than 0
	 * @param CurrentRotation Initial rotation at t = 0
	 * @param CurrentAngularVelocity Initial angular velocity at t = 0
	 * @param TargetRotation The target rotation
	 * @param SmoothingTime The smoothing time
	 * @param SecondsPerPredictionStep How many seconds per prediction step
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static void CriticalSpringDamperQuatPredict(TArrayView<FQuat> OutPredictedRotations, TArrayView<FVector> OutPredictedAngularVelocities,
	                                          int32 PredictCount, const FQuat& CurrentRotation, const FVector& CurrentAngularVelocity,
	                                          const FQuat& TargetRotation, float SmoothingTime, float SecondsPerPredictionStep)
	{
		check(OutPredictedRotations.Num() == PredictCount);
		check(OutPredictedAngularVelocities.Num() == PredictCount);
		
		for (int32 i = 0; i < PredictCount; i++)
		{
			OutPredictedRotations[i] = CurrentRotation;
			OutPredictedAngularVelocities[i] = CurrentAngularVelocity;
		}

		for (int32 i = 0; i < PredictCount; i++)
		{
			const float PredictTime = (float)(i + 1) * SecondsPerPredictionStep; // Note i+1 since we want index 0 to be the first prediction step
			PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
			CriticalSpringDamperQuat(OutPredictedRotations[i], OutPredictedAngularVelocities[i], TargetRotation, SmoothingTime, PredictTime);
			PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
		}
	}

	/** Specialized quaternion damper, similar to FMath::ExponentialSmoothingApprox but for quaternions.
	 *  Smooths a value using exponential damping towards a target.
	 * 
	 * @param InOutRotation			The value to be smoothed
	 * @param InTargetRotation		The target to smooth towards
	 * @param InDeltaTime			Time interval
	 * @param InSmoothingTime		Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr void ExponentialSmoothingApproxQuat(FQuat& InOutRotation, const FQuat& InTargetRotation, const float InDeltaTime, const float InSmoothingTime)
	{
		if (InSmoothingTime > UE_KINDA_SMALL_NUMBER)
		{
			InOutRotation = FQuat::Slerp(InOutRotation, InTargetRotation, 1.0f - FMath::InvExpApprox(InDeltaTime / InSmoothingTime));
		}
		else
		{
			InOutRotation = InTargetRotation;
		}
	}

	/** Specialized angle damper, similar to FMath::ExponentialSmoothingApprox but deals correctly with angle wrap-around.
	 *  Smooths an angle using exponential damping towards a target.
	 *
	 * @param InOutAngleRadians		The angle to be smoothed
	 * @param InTargetAngleRadians	The target to smooth towards
	 * @param InDeltaTime			Time interval
	 * @param InSmoothingTime		Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	static constexpr void ExponentialSmoothingApproxAngle(float& InOutAngleRadians, const float& InTargetAngleRadians, const float InDeltaTime, const float InSmoothingTime)
	{
		if (InSmoothingTime > UE_KINDA_SMALL_NUMBER)
		{
			InOutAngleRadians += FMath::FindDeltaAngleRadians(InOutAngleRadians, InTargetAngleRadians) * (1.0f - FMath::InvExpApprox(InDeltaTime / InSmoothingTime));
		}
		else
		{
			InOutAngleRadians = InTargetAngleRadians;
		}
	}
};
