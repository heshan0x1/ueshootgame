// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Memory/SharedBuffer.h"
#include "Templates/PimplPtr.h"
#include "Templates/SharedPointerFwd.h"
#include "Templates/Tuple.h"
#include "Containers/Array.h"
#include "MetaHumanCharacterIdentity.generated.h"

#define UE_API METAHUMANCORETECHLIB_API

struct FMetaHumanRigEvaluatedState;

namespace dna {
	class Reader;
}

enum class EMetaHumanCharacterOrientation : uint8
{
	Y_UP = 0,
	Z_UP = 1
};

enum class EHeadFitToTargetMeshes : uint8
{
	Head,
	LeftEye,
	RightEye,
	Teeth
};


struct FFloatTriplet;

//! the alignment options used when performing FitToTarget
UENUM(meta = (ScriptName = "MetaHumanAlignmentOptions"))
enum class EAlignmentOptions : uint8
{
	None,
	Translation,
	RotationTranslation,
	ScalingTranslation,
	ScalingRotationTranslation
};

//! the options for performing fit to target: how alignment of the head is performed
USTRUCT(BlueprintType)
struct FFitToTargetOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	EAlignmentOptions AlignmentOptions{ EAlignmentOptions::ScalingRotationTranslation };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	bool bDisableHighFrequencyDelta = true;
};

//! the options used when performing Blend
UENUM()
enum class EBlendOptions : uint8
{
	Proportions,
	Features,
	Both
};

class FMetaHumanCharacterIdentity
{
public:
	UE_API FMetaHumanCharacterIdentity();
	UE_API ~FMetaHumanCharacterIdentity();

	UE_API bool Init(const FString& InMHCDataPath, const FString& InBodyMHCDataPath, class UDNAAsset* InDNAAsset, EMetaHumanCharacterOrientation InDNAAssetOrient);

	class FState;

	UE_API TSharedPtr<FState> CreateState() const;

	class FSettings;


	/** Retrieve all available presets */
	UE_API TArray<FString> GetPresetNames() const;

	/** Copy joint bind poses from body to the face dna. Optionally update descendent joints, default behaviour is for descendent joints to not move in world space. */
	UE_API TSharedPtr<class IDNAReader> CopyBodyJointsToFace(dna::Reader* InBodyDnaReader, dna::Reader* InFaceDnaReader, bool bUpdateDescendentJoints = false) const;

	/** Update skin weights for the overlapping joints in the face from the body and vertex normals */
	UE_API TSharedPtr<class IDNAReader> UpdateFaceSkinWeightsFromBodyAndVertexNormals(const TArray<TPair<int32, TArray<FFloatTriplet>>>& InCombinedBodySkinWeights, dna::Reader* InFaceDnaReader, const FMetaHumanCharacterIdentity::FState& InState) const;

	//! Get the number of mesh vertices for LOD 0, for the specified mesh; returns -1 if fails
	UE_API int32 GetNumLOD0MeshVertices(EHeadFitToTargetMeshes InMeshType) const;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};

class FMetaHumanCharacterIdentity::FSettings
{
public:
	FSettings();
	UE_API ~FSettings();
	FSettings(const FSettings& InOther);

	/** Return the global per vertex delta used when evaluating */
	UE_API float GlobalVertexDeltaScale() const;

	/** Set the global per vertex delta used when evaluating */
	UE_API void SetGlobalVertexDeltaScale(float InGlobalVertexDeltaScale);

	/** Return true if apply body delta when evaluating */
	UE_API bool UseBodyDeltaInEvaluation() const;

	/** Set whether or not we are applying body delta when evaluating */
	UE_API void SetBodyDeltaInEvaluation(bool bInIsBodyDeltaInEvaluation);

	/** Return true if compatibility evaluation is used*/
	UE_API bool UseCompatibilityEvaluation() const;

	/** Set whether to use compatability evaluation for combining body and head*/
	UE_API void SetCompatibilityEvaluation(bool bInIsCompatibilityEvaluation);

	/** Return the global scale used for applying high frequency variant */
	UE_API float GlobalHighFrequencyScale() const;

	/** Set the global scale used for applying high frequency variant */
	UE_API void SetGlobalHighFrequencyScale(float InGlobalHighFrequencyScale);

	/** Set the iterations used when applying high frequency variant */
	UE_API void SetHighFrequencyIteration(int32 InHighFrequencyScale);

	friend class FMetaHumanCharacterIdentity::FState;


private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};


class FMetaHumanCharacterIdentity::FState
{
public:
	UE_API FState();
	UE_API ~FState();
	UE_API FState(const FState& InOther);

	/** Evaluate the DNA vertices and vertex normals based on the state */
	UE_API FMetaHumanRigEvaluatedState Evaluate() const;

	/** Get vertex in UE coordinate system for a specific dna mesh and dna vertex index */
	UE_API FVector3f GetVertex(const TArray<FVector3f>& InVertices, int32 InDNAMeshIndex, int32 InDNAVertexIndex) const;

	/** Get vertex in unconverted for a specific dna mesh and dna vertex index */
	UE_API FVector3f GetRawVertex(const TArray<FVector3f>& InVertices, int32 InDNAMeshIndex, int32 InDNAVertexIndex) const;

	/** Get the raw bind pose (in DNA coord system) */
	UE_API void GetRawBindPose(const TArray<FVector3f>& InVertices, TArray<float>& OutBindPose) const;

	/** Get the coefficients of the underlying model */
	UE_API void GetCoefficients(TArray<float>& OutCoefficients) const;

	/** Get the identifier of the underlying model */
	UE_API void GetModelIdentifier(FString& OutModelIdentifier) const;

	/** Evaluate the Gizmos */
	UE_API TArray<FVector3f> EvaluateGizmos(const TArray<FVector3f>& InVertices) const;

	/** Get the number of gizmos */
	UE_API int32 NumGizmos() const;
	
	/** Evaluate the Landmarks */
	UE_API TArray<FVector3f> EvaluateLandmarks(const TArray<FVector3f>& InVertices) const;

	/** get the number of landmarks */
	UE_API int32 NumLandmarks() const;

	/** Is there a landmark present for the supplied vertex index */
	UE_API bool HasLandmark(int32 InVertexIndex) const ;

	/** Adds a single landmark.  */
	UE_API void AddLandmark(int32 InVertexIndex);

	/** Removes a single landmark for a given landmark index. The landmark index must be in the range 0-NumLandmarks() - 1 */
	UE_API void RemoveLandmark(int32 InLandmarkIndex);

	/** Selects a face vertex given the input ray */
	UE_API int32 SelectFaceVertex(FVector3f InOrigin, FVector3f InDirection, FVector3f& OutVertex, FVector3f& OutNormal);

	/** Reset the face to the archetype */
	UE_API void Reset();

	/** Reset the neck region to the body state */
	UE_API void ResetNeckRegion();

	/** Randomize the face */
	UE_API void Randomize(float InMagnitude);

	/** Create a state based on preset */
	UE_API void GetPreset(const FString& PresetName, int32 InPresetType, int32 InPresetRegion);

	/** Blend region based on preset weights */
	UE_API void BlendPresets(int32 InGizmoIndex, const TArray<TPair<float, const FState*>>& InStates, EBlendOptions InBlendOptions, bool bInBlendSymmetrically);

	/** Set the gizmo position */
	UE_API void SetGizmoPosition(int32 InGizmoIndex, const FVector3f& InPosition, bool bInSymmetric, bool bInEnforceBounds);

	/** Get the gizmo position */
	UE_API void GetGizmoPosition(int32 InGizmoIndex, FVector3f& OutPosition) const;

	/** Get the gizmo position bounds */
	UE_API void GetGizmoPositionBounds(int32 InGizmoIndex, FVector3f& OutMinPosition, FVector3f& OutMaxPosition, float InBBoxReduction, bool bInExpandToCurrent) const;

	/** Set the gizmo rotation */
	UE_API void SetGizmoRotation(int32 InGizmoIndex, const FVector3f& InRotation, bool bInSymmetric, bool bInEnforceBounds);

	/** Get the gizmo rotation */
	UE_API void GetGizmoRotation(int32 InGizmoIndex, FVector3f& OutRotation) const;

	/** Get the gizmo rotation bounds */
	UE_API void GetGizmoRotationBounds(int32 InGizmoIndex, FVector3f& OutMinRotation, FVector3f& OutMaxRotation, bool bInExpandToCurrent) const;

	/** Scale the gizmo */
	UE_API void SetGizmoScale(int32 InGizmoIndex, float InScale, bool bInSymmetric, bool bInEnforceBounds);

	/** Get the gizmo scale */
	UE_API void GetGizmoScale(int32 InGizmoIndex, float& OutScale) const;

	/** Get the gizmo scale bounds */
	UE_API void GetGizmoScaleBounds(int32 InGizmoIndex, float& OutMinScale, float& OutMaxScale, bool bInExpandToCurrent) const;

	/** Translate the landmarks */
	UE_API void TranslateLandmark(int32 InLandmarkIndex, const FVector3f& InDelta, bool bInSymmetric);

	/** Set the face scale relative to the body. */
	UE_API void SetFaceScale(float InFaceScale);

	/** Returns the face scale relative to the body. */
	UE_API float GetFaceScale() const;

	/* Update the face state from body (bind pose, vertices) */
	UE_API void SetBodyJointsAndBodyFaceVertices(const TArray<FMatrix44f>& InBodyJoints, const TArray<FVector3f>& InVertices);

	/* Set the body vertex normals, and an array giving the number of vertices for each lod */
	// TODO perhaps combined this with the method above
	UE_API void SetBodyVertexNormals(const TArray<FVector3f>& InVertexNormals, const TArray<int32>& InNumVerticesPerLod);

	/** Reset the neck exclusion mask */
	UE_API void ResetNeckExclusionMask();

	/** Returns the number of variants for variant of name InVariantName (can be "eyelashes" or "teeth") */
	UE_API int32 GetVariantsCount(const FString& InVariantName) const;

	/** Sets variant of name InVariantName  to State (can be "eyelashes" or "teeth")  **/
	UE_API void SetVariant(const FString& InVariantName, TConstArrayView<float> InVariantWeights);

	/** Set the expression activations for the face state to those defined in the InExpressionActivations map*/	
	UE_API void SetExpressionActivations(TMap<FString, float>& InExpressionActivations);

	/** Returns the maximum number of High Frequency variants supported by the state */
	UE_API int32 GetNumHighFrequencyVariants() const;

	/** Set the high frequency variant to be used by this state. Set <0 for no variant. */
	UE_API void SetHighFrequencyVariant(int32 InHighFrequencyVariant);

	/** Returns the high frequency variant used by this state. */
	UE_API int32 GetHighFrequencyVariant() const;

	/** Gets internal face serialization version */
	UE_API int32 GetInternalSerializationVersion() const;

	/** 
	 * Fit the Character Identity to the map of supplied part vertices (which must contain the Head, but also optionally can contain Eyes and Teeth), using the supplied options.
	 * Note that this leaves the Identity in a state where it needs autorigging. Returns true if successful, false if not
	 */
	UE_API bool FitToTarget(const TMap<int32, TArray<FVector3f>>& InPartsVertices, const FFitToTargetOptions& InFitToTargetOptions);

	/** 
	 * Fit the Character Identity to the supplied DNA, using the supplied options.
	 * Note that this leaves the Identity in a state where it needs autorigging. Returns true if successful, false otherwise (for example if the DNA selected is not appropriate)
	 */
	UE_API bool FitToFaceDna(TSharedRef<class IDNAReader> InFaceDna, const FFitToTargetOptions& InFitToTargetOptions); 

	/** get settings */
	UE_API FMetaHumanCharacterIdentity::FSettings GetSettings() const;

	/** set settings */
	UE_API void SetSettings(const FMetaHumanCharacterIdentity::FSettings& InSettings);

	/** get global scale */
	UE_API bool GetGlobalScale(float& scale) const;

	UE_API void WriteDebugAutoriggingData(const FString& DirectoryPath) const;

	UE_API void Serialize(FSharedBuffer& OutArchive) const;
	UE_API bool Deserialize(const FSharedBuffer& InArchive);

	UE_API TSharedRef<class IDNAReader> StateToDna(dna::Reader* InDnaReader) const;
	UE_API TSharedRef<class IDNAReader> StateToDna(class UDNAAsset* InFaceDNA) const;

	friend class FMetaHumanCharacterIdentity;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};

#undef UE_API
