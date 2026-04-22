// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "PVGravity.generated.h"

namespace PV::Facades
{
	class FPointFacade;
	class FBranchFacade;
}

UENUM()
enum class EGravityMode : uint8
{
	Gravity,
	Phototropic
};

USTRUCT()
struct FPVGravityParams
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category="Gravity", meta=(ToolTip="Selects gravity behavior (bending mode).\n\nChooses how bending is applied: Gravity (downward pull) or Phototropic (growth toward light/up). Affects the direction and style of branch curvature."))
	EGravityMode Mode = EGravityMode::Gravity;

	UPROPERTY(EditAnywhere, Category="Gravity", meta=(ClampMin=0.0f, ClampMax=10.0f, UIMin=0.0f, UIMax=1.0f, Tooltip="Strength of gravity/phototropic effect.\n\nSets how strongly branches bend under gravity or orient toward light in phototropic mode. Higher values yield more pronounced curvature."))
	float Gravity = 0;

	UPROPERTY(EditAnywhere, Category="Gravity", meta=(EditCondition = "Mode == EGravityMode::Gravity", Tooltip="Direction vector used for gravity.\n\nDefines the gravity vector for bending (e.g., world down or a custom direction). Only applies in gravity mode."))
	FVector3f Direction = FVector3f::DownVector;

	UPROPERTY(EditAnywhere, Category="Gravity", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip="Preserves initial branch angle during bending.\n\nBias that resists over-bending by maintaining the original branching angle. Increase to keep initial structure while still responding to forces."))
	float AngleCorrection = 0;

	// UPROPERTY(EditAnywhere, Category="Gravity", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, EditCondition = "Mode == EGravityMode::Phototropic", Tooltip="Bias toward light-facing growth.\n\nBias between optimal light direction and shadow avoidance.  0 Being Light optimal, 1 being shadow avoidance"))
	UPROPERTY()
	float PhototropicBias = 0;
};

struct FPVGravity
{
	static void ApplyGravity(const FPVGravityParams& InGravityParams, FManagedArrayCollection& OutCollection);
	
private :

	static void GeneratePhototropicData(const FPVGravityParams& InGravityParams, const PV::Facades::FBranchFacade& InBranchFacade, const PV::Facades::FPointFacade& InPointFacade, TArray<FVector3f>& OutPhototropicDirections);

	static void ApplyGravity(const int32 BranchIndex, const FPVGravityParams& GravitySettings, const TArray<FVector3f>& PhototropicDirections, FManagedArrayCollection& OutCollection,
		FQuat4f TotalDownForce = FQuat4f::Identity, FVector3f PreviousPosition = FVector3f::ZeroVector, FVector3f PivotPosition = FVector3f::ZeroVector);
};
