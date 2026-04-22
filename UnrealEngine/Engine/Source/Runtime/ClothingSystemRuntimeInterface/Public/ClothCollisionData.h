// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ClothCollisionPrim.h"
#include "Containers/Array.h"
#include "UObject/ObjectMacros.h"
#include "ClothCollisionData.generated.h"

#define UE_API CLOTHINGSYSTEMRUNTIMEINTERFACE_API

USTRUCT()
struct FClothCollisionData
{
	GENERATED_BODY()

	UE_API void Reset();

	UE_API void Append(const FClothCollisionData& InOther);

	UE_API void AppendTransformed(const FClothCollisionData& InOther, const TArray<FTransform>& BoneTransforms);

	/**
	 * Append collision data, but only the individual collision elements that don't already exist as to avoid duplicates.
	 * @Note This is an expensive operation and therefore it is best to call Append() on the first batch of collision data to merge right after a Reset().
	 */
	UE_API void AppendUnique(const FClothCollisionData& InOther);

	bool IsEmpty() const
	{
		return Spheres.IsEmpty() && SphereConnections.IsEmpty() && Convexes.IsEmpty() && Boxes.IsEmpty();
	}

	// Sphere data
	UPROPERTY(EditAnywhere, Category = Collision)
	TArray<FClothCollisionPrim_Sphere> Spheres;

	// Capsule data
	UPROPERTY(EditAnywhere, Category = Collision)
	TArray<FClothCollisionPrim_SphereConnection> SphereConnections;

	// Convex Data
	UPROPERTY(EditAnywhere, Category = Collision)
	TArray<FClothCollisionPrim_Convex> Convexes;

	// Box data
	UPROPERTY(EditAnywhere, Category = Collision)
	TArray<FClothCollisionPrim_Box> Boxes;
};

#undef UE_API
