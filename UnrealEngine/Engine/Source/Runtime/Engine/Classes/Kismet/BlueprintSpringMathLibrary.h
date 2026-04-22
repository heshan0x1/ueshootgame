// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintFunctionLibrary.h"

#include "BlueprintSpringMathLibrary.generated.h"

UCLASS(MinimalAPI, Experimental, meta=(BlueprintThreadSafe))
class UBlueprintSpringMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Simple Spring //

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the value InOutX towards TargetX with the motion of a critically damped spring. The velocity of X is stored in InOutV.
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The speed of the value to be damped
	 * @param TargetX The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutX to TargetX
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampFloat(UPARAM(ref) float& InOutX, UPARAM(ref) float& InOutV, const float& TargetX, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the angle InOutAngle towards TargetAngle with the motion of a critically damped spring. The velocity of InOutAngle is stored in InOutAngularVelocity in deg/s.
	 *
	 * @param InOutAngle The value to be damped in degrees
	 * @param InOutAngularVelocity The speed of the value to be damped in deg/s
	 * @param TargetAngle The goal to damp towards in degrees
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutAngle to TargetX
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta = (Category = "Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampAngle(UPARAM(ref) float& InOutAngle, UPARAM(ref) float& InOutAngularVelocity, const float& TargetAngle, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the value InOutX towards TargetX with the motion of a critically damped spring. The velocity of X is stored in InOutV.
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The speed of the value to be damped
	 * @param TargetX The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutX to TargetX
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampVector(UPARAM(ref) FVector& InOutX, UPARAM(ref) FVector& InOutV, const FVector& TargetX, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the value InOutX towards TargetX with the motion of a critically damped spring. The velocity of X is stored in InOutV.
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The speed of the value to be damped
	 * @param TargetX The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutX to TargetX
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampVector2D(UPARAM(ref) FVector2D& InOutX, UPARAM(ref) FVector2D& InOutV, const FVector2D& TargetX, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the value InOutRotation towards TargetRotation with the motion of a critically damped spring. The velocity of InOutRotation is stored in InOutAngularVelocity in deg/s.
	 *
	 * @param InOutRotation The value to be damped
	 * @param InOutAngularVelocity The speed of the value to be damped in deg/s
	 * @param TargetRotation The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutRotation to TargetRotation
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampQuat(UPARAM(ref) FQuat& InOutRotation, UPARAM(ref) FVector& InOutAngularVelocity, const FQuat& TargetRotation, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Interpolates the value InOutRotation towards TargetRotation with the motion of a critically damped spring. The velocity of InOutRotation is stored in InOutAngularVelocity in deg/s.
	 *
	 * @param InOutRotation The value to be damped
	 * @param InOutAngularVelocity The speed of the value to be damped in deg/s
	 * @param TargetRotation The goal to damp towards
	 * @param SmoothingTime The smoothing time to use for the spring. Longer times lead to more damped behaviour. A time of 0 will snap InOutRotation to TargetRotation
	 * @param DeltaTime Timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void CriticalSpringDampRotator(UPARAM(ref) FRotator& InOutRotation, UPARAM(ref) FVector& InOutAngularVelocity, const FRotator& TargetRotation, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** A velocity spring will damp towards a target that follows a fixed linear target velocity, allowing control of the interpolation speed
	 * while still giving a smoothed behavior. A SmoothingTime of 0 will give a linear interpolation between X and TargetX
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The velocity of the value to be damped
	 * @param InOutVi The intermediate target velocity of the value to be damped
	 * @param TargetX The target value of X to damp towards
	 * @param MaxSpeed The desired speed to achieve while damping towards X
	 * @param SmoothingTime The smoothing time to use while damping towards X. Higher values will give more smoothed behaviour. A value of 0 will give a linear interpolation of X to Target
	 * @param DeltaTime The timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void VelocitySpringDampFloat(UPARAM(ref) float& InOutX, UPARAM(ref) float& InOutV, UPARAM(ref) float& InOutVi, float TargetX, float MaxSpeed, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** A velocity spring will damp towards a target that follows a fixed linear target velocity, allowing control of the interpolation speed
	 * while still giving a smoothed behavior. A SmoothingTime of 0 will give a linear interpolation between X and TargetX
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The velocity of the value to be damped
	 * @param InOutVi The intermediate target velocity of the value to be damped
	 * @param TargetX The target value of X to damp towards
	 * @param MaxSpeed The desired speed to achieve while damping towards X
	 * @param SmoothingTime The smoothing time to use while damping towards X. Higher values will give more smoothed behaviour. A value of 0 will give a linear interpolation of X to Target
	 * @param DeltaTime The timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void VelocitySpringDampVector(UPARAM(ref) FVector& InOutX, UPARAM(ref) FVector& InOutV, UPARAM(ref) FVector& InOutVi, const FVector& TargetX, float MaxSpeed, float DeltaTime,  float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** A velocity spring will damp towards a target that follows a fixed linear target velocity, allowing control of the interpolation speed
	 * while still giving a smoothed behavior. A SmoothingTime of 0 will give a linear interpolation between X and TargetX
	 * 
	 * @param InOutX The value to be damped
	 * @param InOutV The velocity of the value to be damped
	 * @param InOutVi The intermediate target velocity of the value to be damped
	 * @param TargetX The target value of X to damp towards
	 * @param MaxSpeed The desired speed to achieve while damping towards X
	 * @param SmoothingTime The smoothing time to use while damping towards X. Higher values will give more smoothed behaviour. A value of 0 will give a linear interpolation of X to Target
	 * @param DeltaTime The timestep in seconds
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void VelocitySpringDampVector2D(UPARAM(ref) FVector2D& InOutX, UPARAM(ref) FVector2D& InOutV, UPARAM(ref) FVector2D& InOutVi, const FVector2D& TargetX, float MaxSpeed, float DeltaTime,  float SmoothingTime = 0.2f);

	// Damper //

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths a value using exponential damping towards a target.
	 * 
	 * 
	 * @param  Value The value to be smoothed
	 * @param  Target The target to smooth towards
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API float DampFloat(float Value, float Target, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths an angle in degrees using exponential damping towards a target.
	 *
	 *
	 * @param  Angle The angle to be smoothed in degrees
	 * @param  TargetAngle The target to smooth towards in degrees
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta = (Category = "Math|Spring|Experimental"))
	static ENGINE_API float DampAngle(float Angle, float TargetAngle, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths a value using exponential damping towards a target.
	 * 
	 * 
	 * @param  Value The value to be smoothed
	 * @param  Target The target to smooth towards
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API FVector DampVector(const FVector& Value, const FVector& Target, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths a value using exponential damping towards a target.
	 * 
	 * 
	 * @param  Value The value to be smoothed
	 * @param  Target The target to smooth towards
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API FVector2D DampVector2D(const FVector2D& Value, const FVector2D& Target, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths a rotation using exponential damping towards a target.
	 * 
	 * 
	 * @param  Rotation The rotation to be smoothed
	 * @param  TargetRotation The target to smooth towards
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API FQuat DampQuat(const FQuat& Rotation, const FQuat& TargetRotation, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/**
	 * Smooths a value using exponential damping towards a target.
	 * 
	 * 
	 * @param  Rotation The rotation to be smoothed
	 * @param  TargetRotation The target to smooth towards
	 * @param  DeltaTime Time interval
	 * @param  SmoothingTime Timescale over which to smooth. Larger values result in more smoothed behaviour. Can be zero.
	 */
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API FRotator DampRotator(const FRotator& Rotation, const FRotator& TargetRotation, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Update the position of a character given a target velocity using a simple damped spring
	 * 
	 * @param InOutPosition The position of the character
	 * @param InOutVelocity The velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutAcceleration The acceleration of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param TargetVelocity The target velocity of the character.
	 * @param DeltaTime The delta time to tick the character
	 * @param SmoothingTime The time over which to smooth velocity. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void SpringCharacterUpdate(UPARAM(ref) FVector& InOutPosition, UPARAM(ref) FVector& InOutVelocity, UPARAM(ref) FVector& InOutAcceleration, const FVector& TargetVelocity, float DeltaTime, float SmoothingTime = 0.2f);

	UE_EXPERIMENTAL(5.7, "SpringMath is experimental")
	/** Update a position representing a character given a target velocity using a velocity spring.
	 * A velocity spring tracks an intermediate velocity which moves at a maximum acceleration linearly towards a target.
	 * This means unlike the "SpringCharacterUpdate", it will take longer to reach a target velocity that is further away from the current velocity.
	 * 
	 * @param InOutPosition The position of the character
	 * @param InOutVelocity The velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutVelocityIntermediate The intermediate velocity of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param InOutAcceleration The acceleration of the character. Needs to be stored and persisted by the caller. Usually initialized to zero and not modified by the caller.
	 * @param TargetVelocity The target velocity of the character.
	 * @param DeltaTime The delta time to tick the character
	 * @param SmoothingTime The time over which to smooth velocity. It takes roughly the smoothing time in order for the character to reach the target velocity.
	 * @param MaxAcceleration Puts a limit on the maximum acceleration that the intermediate velocity can do each frame. If MaxAccel is very large, the behaviour wil lbe the same as SpringCharacterUpdate
	 */
	UFUNCTION(BlueprintCallable, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API void VelocitySpringCharacterUpdate(UPARAM(ref) FVector& InOutPosition, UPARAM(ref) FVector& InOutVelocity, UPARAM(ref) FVector& InOutVelocityIntermediate, UPARAM(ref) FVector& InOutAcceleration, const FVector& TargetVelocity, float DeltaTime, float SmoothingTime = 0.2f, float MaxAcceleration = 1.0f);

	// Conversion Methods

	/** Convert from smoothing time to spring strength.
	* 
	* @param SmoothingTime The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
	* @return The spring strength. This corresponds to the undamped frequency of the spring in hz.
	*/
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API float ConvertSmoothingTimeToStrength(float SmoothingTime);

	/** Convert from spring strength to smoothing time.
 	* 
 	* @param Strength The spring strength. This corresponds to the undamped frequency of the spring in hz.
 	* @return The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
 	*/
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API float ConvertStrengthToSmoothingTime(float Strength);

	/** Convert a halflife to a smoothing time
 	* 
 	* @param HalfLife The half life of the spring. How long it takes the value to get halfway towards the target.
 	* @return The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate.
 	*/
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API float ConvertHalfLifeToSmoothingTime(float HalfLife);

	/** Convert a smoothing time to a half life
 	* 
 	* @param SmoothingTime The smoothing time of the spring in seconds. It is the time by which the output lags the input when critically damped, when the input is changing at a constant rate. 
 	* @return The half life of the spring. How long it takes the value to get halfway towards the target. 
 	*/
	UFUNCTION(BlueprintPure, meta=(Category="Math|Spring|Experimental"))
	static ENGINE_API float ConvertSmoothingTimeToHalfLife(float SmoothingTime);
};
