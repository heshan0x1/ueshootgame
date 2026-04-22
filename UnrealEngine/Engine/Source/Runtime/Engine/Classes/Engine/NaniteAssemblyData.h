// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "NaniteAssemblyData.generated.h"

class UStaticMesh;

/** A mapping of bone index and weight for bone attachment in Nanite Assemblies */
USTRUCT(BlueprintType)
struct FNaniteAssemblyBoneInfluence
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Default)
	int32 BoneIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Default)
	float BoneWeight = 1.0f;

	bool operator==(const FNaniteAssemblyBoneInfluence& Other) const
	{
		return BoneIndex == Other.BoneIndex && FMath::IsNearlyEqual(BoneWeight, Other.BoneWeight);
	}
	bool operator!=(const FNaniteAssemblyBoneInfluence& Other) const { return !(*this == Other); }
};

/** What space a given assembly node's transform is in */
UENUM(BlueprintType)
enum class ENaniteAssemblyNodeTransformSpace : uint8
{
	// Local (mesh) space
	Local,
	// Relative to the bone influences
	BoneRelative
};

/** A single instance of a given mesh in a Nanite Assembly. */
USTRUCT()
struct FNaniteAssemblyNode
{
	GENERATED_USTRUCT_BODY()
	
	/** The index of the assembly part mesh this node instances */
	UPROPERTY()
	int32 PartIndex = INDEX_NONE;

	/** What space the transform of the node is in */
	UPROPERTY()
	ENaniteAssemblyNodeTransformSpace TransformSpace = ENaniteAssemblyNodeTransformSpace::Local;

	/** The local transform of the node */
	UPROPERTY()
	FTransform3f Transform = FTransform3f::Identity;

	/** The bone index/weight pairs for attachment to a skeleton. NOTE: Should be left empty for static mesh assemblies. */
	UPROPERTY()
	TArray<FNaniteAssemblyBoneInfluence> BoneInfluences;

	bool operator==(const FNaniteAssemblyNode& Other) const
	{
		return PartIndex == Other.PartIndex &&
			TransformSpace == Other.TransformSpace &&
			Transform.Equals(Other.Transform) &&
			BoneInfluences == Other.BoneInfluences;
	}
	bool operator!=(const FNaniteAssemblyNode& Other) const { return !(*this == Other); }
};

/** A mesh to be instanced as a part of a Nanite Assembly */
USTRUCT()
struct FNaniteAssemblyPart
{
	GENERATED_USTRUCT_BODY()

	/** The static mesh to render for the part */
	UPROPERTY(VisibleAnywhere, Category = General)
	FSoftObjectPath MeshObjectPath;

	/** The mapping of the part's materials to the final material list (Empty means material indices map 1:1) */
	UPROPERTY()
	TArray<int32> MaterialRemap;

	bool operator==(const FNaniteAssemblyPart& Other) const
	{
		return MeshObjectPath == Other.MeshObjectPath &&
			MaterialRemap == Other.MaterialRemap;
	}
	bool operator!=(const FNaniteAssemblyPart& Other) const { return !(*this == Other); }
};

/** Data to describe a Nanite Assembly */
USTRUCT()
struct FNaniteAssemblyData
{
	GENERATED_USTRUCT_BODY()

	/** The list of assembly parts */
	UPROPERTY(VisibleAnywhere, Category = General)
	TArray<FNaniteAssemblyPart> Parts;

	/** The list of assembly part instance nodes */
	UPROPERTY()
	TArray<FNaniteAssemblyNode> Nodes;

	const bool IsValid() const { return Parts.Num() > 0 && Nodes.Num() > 0; }
	bool operator==(const FNaniteAssemblyData& Other) const {  return Parts == Other.Parts && Nodes == Other.Nodes; }
	bool operator!=(const FNaniteAssemblyData& Other) const { return !(*this == Other); }

	void SerializeForDDC(FArchive& Ar)
	{
		bool bValid = IsValid();
		Ar << bValid;
		if (bValid)
		{
			for (FNaniteAssemblyPart& Part : Parts)
			{
				FString Path = Part.MeshObjectPath.ToString();
				Ar << Path;
				Ar << Part.MaterialRemap;
			}
			Ar << Nodes;
		}

	}
};

/** Serializes a FNaniteAssemblyBoneInfluence */
inline FArchive& operator<<(FArchive& Ar, FNaniteAssemblyBoneInfluence& Influence)
{
	Ar << Influence.BoneIndex;
	Ar << Influence.BoneWeight;

	return Ar;
}

/** Serializes a FNaniteAssemblyNode */
inline FArchive& operator<<(FArchive& Ar, FNaniteAssemblyNode& Node)
{
	Ar << Node.PartIndex;
	Ar << Node.TransformSpace;
	Ar << Node.Transform;
	Ar << Node.BoneInfluences;

	return Ar;
}

/** Serializes a FNaniteAssemblyPart */
inline FArchive & operator<<(FArchive& Ar, FNaniteAssemblyPart& Part)
{
	Ar << Part.MeshObjectPath;
	Ar << Part.MaterialRemap;

	return Ar;
}

/** Serializes a FNaniteAssemblyData */
inline FArchive& operator<<(FArchive& Ar, FNaniteAssemblyData& Data)
{
	Ar << Data.Parts;
	Ar << Data.Nodes;
	
	return Ar;
}