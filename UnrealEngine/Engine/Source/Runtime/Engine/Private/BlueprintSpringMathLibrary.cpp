// Copyright Epic Games, Inc. All Rights Reserved.

#include "Kismet/BlueprintSpringMathLibrary.h"

#include "Animation/SpringMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintSpingMathLibrary, Log, All);

void UBlueprintSpringMathLibrary::CriticalSpringDampVector(FVector& InOutX, FVector& InOutV, const FVector& TargetX, float DeltaTime, float SmoothingTime)
{
	SpringMath::CriticalSpringDamper(InOutX, InOutV, TargetX, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::CriticalSpringDampVector2D(FVector2D& InOutX, FVector2D& InOutV, const FVector2D& TargetX, float DeltaTime, float SmoothingTime)
{
	SpringMath::CriticalSpringDamper(InOutX, InOutV, TargetX, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::CriticalSpringDampFloat(float& InOutX, float& InOutV, const float& TargetX, float DeltaTime, float SmoothingTime)
{
	SpringMath::CriticalSpringDamper(InOutX, InOutV, TargetX, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::CriticalSpringDampAngle(float& InOutAngle, float& InOutAngularVelocity, const float& TargetAngle, float DeltaTime, float SmoothingTime)
{
	float InOutAngleRadians = FMath::DegreesToRadians(InOutAngle);
	float InOutAngularVelocityRadians = FMath::DegreesToRadians(InOutAngularVelocity);
	SpringMath::CriticalSpringDamperAngle(InOutAngleRadians, InOutAngularVelocityRadians, FMath::DegreesToRadians(TargetAngle), SmoothingTime, DeltaTime);
	InOutAngle = FMath::RadiansToDegrees(InOutAngleRadians);
	InOutAngularVelocity = FMath::RadiansToDegrees(InOutAngularVelocityRadians);
}

void UBlueprintSpringMathLibrary::CriticalSpringDampQuat(FQuat& InOutRotation, FVector& InOutAngularVelocity, const FQuat& TargetRotation,
	float DeltaTime, float SmoothingTime)
{
	FVector InOutAngularVelocityRadians = FMath::DegreesToRadians(InOutAngularVelocity);
	SpringMath::CriticalSpringDamperQuat(InOutRotation, InOutAngularVelocityRadians, TargetRotation, SmoothingTime, DeltaTime);
	InOutAngularVelocity = FMath::RadiansToDegrees(InOutAngularVelocityRadians);
}

void UBlueprintSpringMathLibrary::CriticalSpringDampRotator(FRotator& InOutRotation, FVector& InOutAngularVelocity, const FRotator& TargetRotation,
	float DeltaTime, float SmoothingTime)
{
	FQuat InOutRotationQuat = InOutRotation.Quaternion();
	FVector InOutAngularVelocityRadians = FMath::DegreesToRadians(InOutAngularVelocity);
	SpringMath::CriticalSpringDamperQuat(InOutRotationQuat, InOutAngularVelocityRadians, TargetRotation.Quaternion(), SmoothingTime, DeltaTime);
	InOutRotation = InOutRotationQuat.Rotator();
	InOutAngularVelocity = FMath::RadiansToDegrees(InOutAngularVelocityRadians);
}

void UBlueprintSpringMathLibrary::VelocitySpringDampFloat(float& InOutX, float& InOutV, float& InOutVi, float TargetX, float MaxSpeed,
                                                          float DeltaTime, float SmoothingTime)
{
	if (MaxSpeed < 0.0f)
	{
		UE_LOG(LogBlueprintSpingMathLibrary, Warning, TEXT("UBlueprintSpringMathLibrary::VelocitySpringDampFloat TargetSpeed cannot be negative"));
		return;
	}

	SpringMath::VelocitySpringDamperF(InOutX, InOutV, InOutVi, TargetX, MaxSpeed, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::VelocitySpringDampVector(FVector& InOutX, FVector& InOutV, FVector& InOutVi, const FVector& TargetX, float MaxSpeed,
	float DeltaTime, float SmoothingTime)
{
	if (MaxSpeed < 0.0f)
	{
		UE_LOG(LogBlueprintSpingMathLibrary, Warning, TEXT("UBlueprintSpringMathLibrary::VelocitySpringDampVector TargetSpeed cannot be negative"));
		return;
	}

	SpringMath::VelocitySpringDamper(InOutX, InOutV, InOutVi, TargetX, MaxSpeed, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::VelocitySpringDampVector2D(FVector2D& InOutX, FVector2D& InOutV, FVector2D& InOutVi, const FVector2D& TargetX,
	float MaxSpeed, float DeltaTime, float SmoothingTime)
{
	if (MaxSpeed < 0.0f)
	{
		UE_LOG(LogBlueprintSpingMathLibrary, Warning, TEXT("UBlueprintSpringMathLibrary::VelocitySpringDampVector2D TargetSpeed cannot be negative"));
		return;
	}

	SpringMath::VelocitySpringDamper(InOutX, InOutV, InOutVi, TargetX, MaxSpeed, SmoothingTime, DeltaTime);
}

float UBlueprintSpringMathLibrary::DampFloat(float Value, float Target, float DeltaTime, float SmoothingTime)
{
	float DampedValue = Value;
	FMath::ExponentialSmoothingApprox(DampedValue, Target, DeltaTime, SmoothingTime);
	return DampedValue;
}

float UBlueprintSpringMathLibrary::DampAngle(float Angle, float TargetAngle, float DeltaTime, float SmoothingTime)
{
	float DampedAngle = FMath::DegreesToRadians(Angle);
	SpringMath::ExponentialSmoothingApproxAngle(DampedAngle, FMath::DegreesToRadians(TargetAngle), DeltaTime, SmoothingTime);
	return FMath::RadiansToDegrees(DampedAngle);
}

FVector UBlueprintSpringMathLibrary::DampVector(const FVector& Value, const FVector& Target, float DeltaTime, float SmoothingTime)
{
	FVector DampedValue = Value;
	FMath::ExponentialSmoothingApprox(DampedValue, Target, DeltaTime, SmoothingTime);
	return DampedValue;
}

FVector2D UBlueprintSpringMathLibrary::DampVector2D(const FVector2D& Value, const FVector2D& Target, float DeltaTime, float SmoothingTime)
{
	FVector2D DampedValue = Value;
	FMath::ExponentialSmoothingApprox(DampedValue, Target, DeltaTime, SmoothingTime);
	return DampedValue;
}

FQuat UBlueprintSpringMathLibrary::DampQuat(const FQuat& Rotation, const FQuat& TargetRotation, float DeltaTime, float SmoothingTime)
{
	FQuat DampedRotation = Rotation;
	SpringMath::ExponentialSmoothingApproxQuat(DampedRotation, TargetRotation, DeltaTime, SmoothingTime);
	return DampedRotation;
}

FRotator UBlueprintSpringMathLibrary::DampRotator(const FRotator& Rotation, const FRotator& TargetRotation, float DeltaTime, float SmoothingTime)
{
	FQuat DampedRotation = Rotation.Quaternion();
	SpringMath::ExponentialSmoothingApproxQuat(DampedRotation, TargetRotation.Quaternion(), DeltaTime, SmoothingTime);
	return DampedRotation.Rotator();
}

void UBlueprintSpringMathLibrary::SpringCharacterUpdate(FVector& InOutPosition, FVector& InOutVelocity, FVector& InOutAcceleration,
                                                        const FVector& TargetVelocity, float DeltaTime, float SmoothingTime)
{
	SpringMath::SpringCharacterUpdate(InOutPosition, InOutVelocity, InOutAcceleration, TargetVelocity, SmoothingTime, DeltaTime);
}

void UBlueprintSpringMathLibrary::VelocitySpringCharacterUpdate(FVector& InOutPosition, FVector& InOutVelocity, FVector& InOutVelocityIntermediate,
	FVector& InOutAcceleration, const FVector& TargetVelocity, float DeltaTime, float SmoothingTime, float MaxAcceleration)
{
	SpringMath::VelocitySpringCharacterUpdate(InOutPosition, InOutVelocity, InOutVelocityIntermediate, InOutAcceleration, TargetVelocity, SmoothingTime, MaxAcceleration, DeltaTime);
}

float UBlueprintSpringMathLibrary::ConvertSmoothingTimeToStrength(float SmoothingTime)
{
	return SpringMath::SmoothingTimeToStrength(SmoothingTime);
}

float UBlueprintSpringMathLibrary::ConvertStrengthToSmoothingTime(float Strength)
{
	return SpringMath::StrengthToSmoothingTime(Strength);
}

float UBlueprintSpringMathLibrary::ConvertHalfLifeToSmoothingTime(float HalfLife)
{
	return SpringMath::HalfLifeToSmoothingTime(HalfLife);
}

float UBlueprintSpringMathLibrary::ConvertSmoothingTimeToHalfLife(float SmoothingTime)
{
	return SpringMath::SmoothingTimeToHalfLife(SmoothingTime);
}
