// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumClassFlags.h"
#include "Misc/NotNull.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "SkelMeshDNAUtils.h"

#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCharacterBodyIdentity.h"

class IDNAReader;
class USkeletalMesh;
enum class EMetaHumanImportDNAType : uint8;
enum class EMetaHumanCharacterTemplateType : uint8;

namespace UE::MetaHuman
{
	struct FMergedMeshMapping;
}

enum class EVertexPositionsAndNormals : uint8
{
	PositionOnly, 	// vertex positions only
	NormalsOnly,	// vertex normals only
	Both			// both vertex positions and normals
};

// Helper with utility functions to modify Skeletal Mesh assets
struct FMetaHumanCharacterSkelMeshUtils
{
	enum class EUpdateFlags : uint32
	{
		None = 0,
		BaseMesh = 1 << 0,
		Joints = 1 << 1,
		SkinWeights = 1 << 2,
		DNABehavior = 1 << 3,
		DNAGeometry = 1 << 4,
		//MorphTargets = 1 << 6, // TODO
		All = MAX_uint32
	};
	FRIEND_ENUM_CLASS_FLAGS(EUpdateFlags);

	/**
	 * Updates the input Skeleta Mesh with the DNA data pass by the reader.
	 * The update flags specifies which DNA info need to be updated, the rest are ignored.
	 * 
	 * Note that the Skeletal Mesh will be re-build and the DNAToSkelMeshMap is updated to match the latest render data
	 *
	 * InOutDNAToSkelMeshMap should be a valid map for the current input DNA reader and skel mesh, see USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh()
	 */
	static void UpdateSkelMeshFromDNA(
		TSharedRef<IDNAReader> InDNAReader, 
		EUpdateFlags InUpdateFlags, 
		TSharedRef<FDNAToSkelMeshMap>& InOutDNAToSkelMeshMap, 
		EMetaHumanCharacterOrientation InCharacterOrientation,
		TNotNull<USkeletalMesh*> OutSkeletalMesh);

	/**
	 * Updates and commits the Mesh Description of the input Skeletal Mesh for Import Model LODs of the mesh 
	 */
	static void UpdateMeshDescriptionFromLODModel(TNotNull<USkeletalMesh*> InSkeletalMesh);

	/**
	 * Updates the vertex, normal, and tangent positions of the input mesh descriptions based on the input Skeletal Mesh Import Model LODs
	 * 
	 * The method uses a subset of 
	 * void FSkeletalMeshLODModel::GetMeshDescription(const USkeletalMesh *InSkeletalMesh, const int32 InLODIndex, FMeshDescription& OutMeshDescription) const
	 * where instead of recreating the mesh description from scratch, vertices, normals, and tangents are updated. If the mesh description
	 * does not match the LOD model, then it falls back to a full MeshDescription update using GetMeshDescription()
	 */
	static void UpdateMeshDescriptionFromLODModelVerticesNormalsAndTangents(TNotNull<USkeletalMesh*> InSkeletalMesh);

	/**
	 * Compares the vertex positions of each LOD in the input DNA and the Skeletal Mesh
	 */
	static bool CompareDnaToSkelMeshVertices(
		TSharedPtr<const IDNAReader> InDNAReader, 
		TNotNull<const USkeletalMesh*> InSkeletalMesh, 
		const FDNAToSkelMeshMap& InDNAToSkelMeshMap, 
		float Tolerance = UE_KINDA_SMALL_NUMBER);

	/**
	 * Compares the vertex and normals positions of each LOD in the input DNA and the Skeletal Mesh
	 * Note that the face state is evaluated internally
	 */
	static bool CompareDnaToStateVerticesAndNormals(
		TSharedPtr<const IDNAReader> InDNAReader, 
		const TArray<FVector3f>& InStateVertices,
		const TArray<FVector3f>& InStateNormals,
		TSharedPtr<const FMetaHumanCharacterIdentity::FState> InState,
		float Tolerance = UE_KINDA_SMALL_NUMBER);


	// Copy from the FDNAUtilities::CheckDNACompatibility in MetaHuman plugin
	static bool CheckDNACompatibility(IDNAReader* InDnaReaderA, IDNAReader* InDnaReaderB);

	// Function overloads for body & face states to update the vertex positions of the Import Model in the input Skeletal Mesh
	// based on the input (evaluated) vertices of the state.
	//
	// If a merged head and body mesh is provided, this will also be updated.
	static void UpdateLODModelVertexPositions(
		TNotNull<USkeletalMesh*> InSkelMesh,
		const FMetaHumanRigEvaluatedState& InVerticesAndNormals,
		const FMetaHumanCharacterIdentity::FState& InState,
		const FDNAToSkelMeshMap& InDNAToSkelMeshMap,
		ELodUpdateOption InUpdateOption, 
		EVertexPositionsAndNormals InVertexUpdateOption,
		TObjectPtr<USkeletalMesh> InMergedHeadAndBody = nullptr,
		const UE::MetaHuman::FMergedMeshMapping* InMergedMeshMapping = nullptr,
		bool bInUpdateMergedMeshNormals = false);

	static void UpdateLODModelVertexPositions(
		TNotNull<USkeletalMesh*> InSkelMesh,
		const FMetaHumanRigEvaluatedState& InVerticesAndNormals,
		const FMetaHumanCharacterBodyIdentity::FState& InState,
		const FDNAToSkelMeshMap& InDNAToSkelMeshMap,
		ELodUpdateOption InUpdateOption, 
		EVertexPositionsAndNormals InVertexUpdateOption,
		TObjectPtr<USkeletalMesh> InMergedHeadAndBody = nullptr,
		const UE::MetaHuman::FMergedMeshMapping* InMergedMeshMapping = nullptr,
		bool bInUpdateMergedMeshNormals = false);

	static void UpdateBindPoseFromSource(TNotNull<USkeletalMesh*> InSourceSkelMesh,	TNotNull<USkeletalMesh*> InTargetSkelMesh);

	static void PopulateSkelMeshData(TNotNull<USkeletalMesh*> InSkelMesh, TSharedPtr<IDNAReader> InDNAReader, bool bIsFaceMesh);

	static void EnableRecomputeTangents(TNotNull<USkeletalMesh*> InSkelMesh);

	static TArray<FVector3f> GetComponentSpaceJointTranslations(TNotNull<USkeletalMesh*> InSkelMesh);

	/** Get vertices from template static mesh in vertex order of the mesh description */
	static bool GetStaticMeshVertices(const TNotNull<UStaticMesh*> InTemplateStaticMesh, int32 InLodIndex, TArray<FVector3f>& OutVertices);

	/** Get vertices from template skeletal mesh, using DNAToSkelMeshMap to map vertex order from source model */
	static bool GetSkeletalMeshVertices(
		const TNotNull<USkeletalMesh*> InTemplateSkeletalMesh,
		int32 InLodIndex,
		const TNotNull<FDNAToSkelMeshMap*> InDNAToSkelMeshMap,
		int32 InDNAMeshIndex,
		TArray<FVector3f>& OutVertices);

	/**
	 * Gets corresponding vertices from template static mesh in the vertex order of the DNA archetype.
	 * Finds corresponding vertices by matching UVs.
	 * UV map of template mesh and DNA archetype should match and should have non-overlapping UVs with each part of the mesh its unique UV space.
	 */
	static bool GetUVCorrespondingStaticMeshVertices(
		const TNotNull<UStaticMesh*> InTemplateStaticMesh,
		int32 InLodIndex,
		const TSharedPtr<IDNAReader>& InArchetypeDNAReader,
		int32 InDNAMeshIndex,
		TArray<FVector3f>& OutVertices);

	/**
	* Gets corresponding vertices from template skeletal mesh in the vertex order of the DNA archetype.
	* Finds corresponding vertices by matching UVs.
	* UV map of template mesh and DNA archetype should match and should have non-overlapping UVs with each part of the mesh its unique UV space.
	*/
	static bool GetUVCorrespondingSkeletalMeshVertices(
		const TNotNull<USkeletalMesh*> InTemplateSkelMesh,
		int32 InLodIndex,
		const TNotNull<FDNAToSkelMeshMap*> InDNAToSkelMeshMap,
		const TSharedPtr<IDNAReader>& InArchetypeDNAReader,
		int32 InDNAMeshIndex,
		TArray<FVector3f>& OutVertices);

	/**
	 * Create a USkeletalMesh asset with provided path using the dna interchange system for a given dna data and name.
	 * The Skeletal mesh is created of a specified type and corresponding Skeleton is selected
	 */
	static USkeletalMesh* GetSkeletalMeshAssetFromDNA(const TSharedPtr<IDNAReader>& InDNAReader, const FString& InAssetPath, const FString& InAssetName, const EMetaHumanImportDNAType InImportDNAType);

	static USkeletalMesh* CreateArchetypeSkelMeshFromDNA(const EMetaHumanImportDNAType InImportDNAType, TSharedPtr<IDNAReader>& OutArchetypeDnaReader);

	static UDNAAsset* GetArchetypeDNAAseet(const EMetaHumanImportDNAType InImportDNAType, UObject* InOuter);
	
	static FString GetTransientArchetypeMeshAssetName(const EMetaHumanImportDNAType InImportDNAType);

	static class UPhysicsAsset* GetFaceArchetypePhysicsAsset(EMetaHumanCharacterTemplateType InTemplateType);
	static class USkeletalMeshLODSettings* GetFaceArchetypeLODSettings(EMetaHumanCharacterTemplateType InTemplateType);
	static class UControlRigBlueprint* GetFaceArchetypeDefaultAnimatingRig(EMetaHumanCharacterTemplateType InTemplateType);

	static class UPhysicsAsset* GetBodyArchetypePhysicsAsset(EMetaHumanCharacterTemplateType InTemplateType);
	static class USkeletalMeshLODSettings* GetBodyArchetypeLODSettings(EMetaHumanCharacterTemplateType InTemplateType);
	static class UControlRigBlueprint* GetBodyArchetypeDefaultAnimatingRig(EMetaHumanCharacterTemplateType InTemplateType);
};

ENUM_CLASS_FLAGS(FMetaHumanCharacterSkelMeshUtils::EUpdateFlags);
