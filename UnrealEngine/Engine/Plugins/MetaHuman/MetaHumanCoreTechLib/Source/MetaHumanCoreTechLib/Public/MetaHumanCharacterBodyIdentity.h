// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DNAAsset.h"
#include "MetaHumanBodyType.h"
#include "Memory/SharedBuffer.h"
#include "MetaHumanCharacterBodyIdentity.generated.h"

#define UE_API METAHUMANCORETECHLIB_API

struct FMetaHumanRigEvaluatedState;

UENUM(BlueprintType)
enum class EBodyBlendOptions : uint8
{
	Skeleton UMETA(Tooltip="Blends only skeletal proportions, enabling proportion changes without altering shaping"),
	Shape UMETA(Tooltip="Blends only shaping, allowing adjustments without affecting skeletal proportions"),
	Both UMETA(Tooltip="Blends both skeletal proportions and shaping simultaneously"),
};


UENUM(BlueprintType)
enum class EMetaHumanCharacterBodyFitOptions : uint8
{
	FitFromMeshOnly				UMETA(Tooltip="Uses mesh only from the DNA file"),
	FitFromMeshAndSkeleton		UMETA(Tooltip="Uses mesh and core (animation) skeleton from the DNA file"),
	FitFromMeshToFixedSkeleton	UMETA(Tooltip="Uses mesh from the DNA file and the core (animation) skeleton from the current MHC state")
};

USTRUCT(BlueprintType)
struct FConformBodyParams
{
	GENERATED_BODY()

	/** When disabled, will import core joints only. When enabled, will import core joints and helper joints */
	UPROPERTY(EditAnywhere, Category = "Import Joints")
	bool bImportHelperJoints = true;

	/** Gives much better conform if the target is already posed in meta human A-pose */
	UPROPERTY(EditAnywhere, Category = "Conform", DisplayName = "Target mesh is in Metahuman A-pose")
	bool bTargetIsInMetaHumanAPose = true;

	/** Estimate joints volumetrically from mesh vertices */
	UPROPERTY(EditAnywhere, Category = "Conform", AdvancedDisplay)
	bool bEstimateJointsFromMesh = false;

	/** When disabled, current helper joint positions and RBF weights will be preserved.
	 * When enabled, helper joints will be repositioned to fit the new mesh, and RBF weights will be updated. */
	UPROPERTY(EditAnywhere, Category = "Import Mesh")
	bool bAutoRigHelperJoints = true;

};

class FMetaHumanCharacterBodyIdentity
{
public:
	UE_API FMetaHumanCharacterBodyIdentity();
	UE_API ~FMetaHumanCharacterBodyIdentity();

	UE_API bool Init(const FString& InPCAModelPath, const FString& InLegacyBodiesPath);

	/* get the number of vertices in the body model for LOD0, either for the body model or the combined body model */
	UE_API int32 GetNumLOD0MeshVertices(bool bInCombined) const;

	/* get mapping from body mesh to combined mesh */
	UE_API TArray<int32> GetBodyToCombinedMapping() const;
	
	class FState;
	UE_API TSharedPtr<FState> CreateState() const;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterBodyConstraint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Constraint")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	bool bIsActive = false;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	float TargetMeasurement = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Constraint")
	float MinMeasurement = 50.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Constraint")
	float MaxMeasurement = 50.0f;
};

struct PhysicsBodyVolume
{
	FVector Center;
	FVector Extent;
};

//! a Simple struct representing an Eigen::Triplet in UE types which can be mem copied from Eigen::Triplet<float>
struct FFloatTriplet
{
	int32 Row;
	int32 Col;
	float Value;
};

class FMetaHumanCharacterBodyIdentity::FState
{
public:
	UE_API FState();
	UE_API ~FState();
	UE_API FState(const FState& InOther);

	/** Get the body constraints from the model */
	UE_API TArray<FMetaHumanCharacterBodyConstraint> GetBodyConstraints(bool bScaleMeasurementRangesWithHeight = false) const;

	/** Set the body constraints and evaluate the DNA vertices based on the state */
	UE_API void EvaluateBodyConstraints(const TArray<FMetaHumanCharacterBodyConstraint>& BodyConstraints);

	/* Get the DNA vertices and vertex normals from the state */
	UE_API FMetaHumanRigEvaluatedState GetVerticesAndVertexNormals() const;

	/* Get the number of vertices per LOD */
	UE_API TArray<int32> GetNumVerticesPerLOD() const;

	/** Get vertex in UE coordinate system for a specific dna mesh and dna vertex index */
	UE_API FVector3f GetVertex(const TArray<FVector3f>& InVertices, int32 InDNAMeshIndex, int32 InDNAVertexIndex) const;

	/** Get gizmo positions used for blending regions */
	UE_API TArray<FVector3f> GetRegionGizmos() const;

	/** Blend region based on preset weights */
	UE_API void BlendPresets(int32 InGizmoIndex, const TArray<TPair<float, const FState*>>& InStates, EBodyBlendOptions InBodyBlendOptions);

	/** Get the number of constraints from the model */
	UE_API int32 GetNumberOfConstraints() const;

	/* Get the actual measurement on the mesh for a particular constraint */
	UE_API float GetMeasurement(int32 ConstraintIndex) const;

	/** Obtains measurements map (string to float) for given face and body DNAs */
	UE_API void GetMeasurementsForFaceAndBody(TSharedRef<IDNAReader> InFaceDNA, TSharedRef<IDNAReader> InBodyDNA, TMap<FString, float>& OutMeasurements) const;

	/** Obtains measurements map (string to float) for body state with given face vertices*/
	UE_API void GetMeasurementsForFaceAndBody(const TArray<FVector3f>& InFaceRawVertices, TMap<FString, float>& OutMeasurements) const;

	/* Get the contour vertex positions on the mesh for a particular constraint */
	UE_API TArray<FVector> GetContourVertices(int32 ConstraintIndex) const;

	/* Copy the bind pose transforms */
	UE_API TArray<FMatrix44f> CopyBindPose() const;

	UE_API int32 GetNumberOfJoints() const;
	UE_API void GetNeutralJointTransform(int32 JointIndex, FVector3f& OutJointTranslation, FRotator3f& OutJointRotation) const;

	/* Copy the combined body model skinning weights as an array of triplets which can be used to reconstruct a sparse matrix of skinning weights*/
	UE_API void CopyCombinedModelVertexInfluenceWeights(TArray<TPair<int32, TArray<FFloatTriplet>>> & OutCombinedModelVertexInfluenceWeights) const;

	/* Copy the combined body model skinning weights as an array of triplets for LOD0*/
	UE_API void SetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> InCombinedModelVertexInfluenceWeightsLOD0);

	/* Copy the combined body model skinning weights as an array of triplets for LOD0*/
	UE_API void GetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> & OutCombinedModelVertexInfluenceWeightsLOD0) const;

	/** Reset the body to the archetype */
	UE_API void Reset();

	/** Get MetaHuman body type */
	UE_API EMetaHumanBodyType GetMetaHumanBodyType() const;

	/** Set MetaHuman body type */
	UE_API void SetMetaHumanBodyType(EMetaHumanBodyType InMetaHumanBodyType, bool bFitFromLegacy = false);

#if WITH_EDITORONLY_DATA
	/* Fit the Character to the supplied DNA */
	UE_DEPRECATED(5.7, "FitToBodyDna with EMetaHumanCharacterBodyFitOptions has been deprecated, please use FitToBodyDna with FConformBodyParams instead.")
	UE_API bool FitToBodyDna(TSharedRef<class IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);

	/* Fit the Character to the supplied vertices */
	UE_DEPRECATED(5.7, "FitToTarget with EMetaHumanCharacterBodyFitOptions has been deprecated, please use FitToTarget with FConformBodyParams instead.")
	UE_API bool FitToTarget(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InComponentJointTranslations, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);
#endif // WITH_EDITORONLY_DATA

	/* Conforms the model to target parameters */
	UE_API bool Conform(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bTargetIsInAPose, bool bEstimateJointsFromMesh);
	
	/* Set custom joint positions */
	UE_API bool SetJointTranslations(const TArray<FVector3f>& InComponentJointTranslations, bool bImportHelperJoints);

	/* Set custom joint rotations*/
	UE_API bool SetJointRotations(const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints);
	
	/* Get joint positions */
	UE_API TArray<FVector3f> GetJointTranslations() const;
	
	/* Set custom mesh */
	UE_API bool SetMesh(const TArray<FVector3f>& InVertices, bool bRepositionHelperJoints);

	/* Get and set the body vertex and joint global delta scale */
	UE_API float GetGlobalDeltaScale() const;
	UE_API void SetGlobalDeltaScale(float InVertexDelta);

	/** Serialize/Deserialize */
	UE_API bool Serialize(FSharedBuffer& OutArchive) const;
	UE_API bool Deserialize(const FSharedBuffer& InArchive);

	/** Create updated dna from state */
	UE_API TSharedRef<IDNAReader> StateToDna(dna::Reader* InDnaReader, bool bIsCombined = false) const;

	UE_API TSharedRef<IDNAReader> StateToDna(UDNAAsset* InBodyDna) const;

	/* Get the list of physics volumes for a joint */
	UE_API TArray<PhysicsBodyVolume> GetPhysicsBodyVolumes(const FName& InJointName) const;

	friend class FMetaHumanCharacterBodyIdentity;
	friend class FMetaHumanCharacterIdentity;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};

#undef UE_API
