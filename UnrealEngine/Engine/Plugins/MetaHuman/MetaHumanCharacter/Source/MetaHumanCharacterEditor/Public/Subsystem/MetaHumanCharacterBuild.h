// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Features/IModularFeatures.h"
#include "MetaHumanCharacterAssemblySettings.h"
#include "MetaHumanTypesEditor.h"
#include "Misc/NotNull.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPtr.h"
#include "SkelMeshDNAUtils.h"

#include "MetaHumanCharacterBuild.generated.h"

/**
 * Parameters to configure MetaHuman Character build.
 */
USTRUCT(BlueprintType)
struct METAHUMANCHARACTEREDITOR_API FMetaHumanCharacterEditorBuildParameters
{
	GENERATED_BODY()

	/**
	 * Which type of build to perform.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build")
	EMetaHumanDefaultPipelineType PipelineType = EMetaHumanDefaultPipelineType::Cinematic;

	/**
	 * The quality of build to perform.
	 * Only relevant when PipelineType is Optimized or UEFN
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build")
	EMetaHumanQualityLevel PipelineQuality = EMetaHumanQualityLevel::Cinematic;

	/** Target animation system name to be used by the assembled MetaHuman. */
	UPROPERTY(BlueprintReadWrite, Category = "Build")
	FName AnimationSystemName;

	/**
	 * Absolute location where the built assets will end up in. If empty, build will unpack
	 * assets in respect to the options set by the palette.
	 * 
	 * Only relevant when Pipeline type is Cinematic, Optimized or DCC
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build")
	FString AbsoluteBuildPath;

	/**
	 * Optional string to be used instead of the character name for the final unpacking folder.
	 * Only relevant when Pipeline type is Cinematic, Optimized or DCC with bExportZipFile set to true
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build")
	FString NameOverride;

	/**
	 * Optional path to a directory where Common MH assets should be shared or copied if needed.
	 * Only relevant when PipelineType is Cinematic or Optimized
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build|Default")
	FString CommonFolderPath;

	/**
	 * Whether or not to bake makeup in the face textures when exporting to DCC
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build|DCC")
	bool bBakeMakeup = true;

	/**
	 * When PipelineType is DCC, export a zip file instead of a folder.
	 * Use NameOverride to change the name of the zip file. By default, it will use the character name.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build|DCC")
	bool bExportZipFile = false;

	/**
	 * If disabled, won't run validation of wardrobe items, allowing potentially incompatible items
	 * to be present in the assembled MetaHuman. Setting this to false bEnableWardrobeItemValidation
	 * defined in the Project Settings.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Build|Validation")
	bool bEnableWardrobeItemValidation = true;

	/**
	 * Specifies the pipeline override to use to build the character.
	 *
	 * If none, build will fall back on using the pipeline defined on the MetaHumanCharacter,
	 * otherwise it will try to use the given pipeline class.
	 */
	UPROPERTY()
	TObjectPtr<class UMetaHumanCollectionPipeline> PipelineOverride;
};

class METAHUMANCHARACTEREDITOR_API IMetaHumanCharacterBuildExtender : public IModularFeature
{
public:
	virtual ~IMetaHumanCharacterBuildExtender() = default;
	static inline const FName FeatureName = TEXT("MetaHumanCharacterBuildExtender");

	// Additional mount points so assets get considered by assembly.
	virtual TSet<FString> GetMountPoints() const { return {}; }

	// Additional package root path like a plugin path to copy assets into Common. Assets within that package will be copied over to the project common folder.
	virtual TSet<FName> GetPackageRootsToCopyFrom() const { return {}; }

	/** Get the additional animation system names the extender will add. */
	virtual TArray<FName> GetAnimationSystemOptions() const { return {}; }

	/** Inform the extender about the Common folder path. */
	virtual void SetCommonPath(const FString& InCommonFolderPath) {}

	// Manually copied, additional assets for cases where dependencies are created after duplication in the assembly process or there is no dependency from the actor blueprint at all.
	virtual TArray<UObject*> GetRootObjects(const FMetaHumanCharacterEditorBuildParameters& BuildParameters) const { return {}; }
};

namespace UE::MetaHuman
{
	// A mapping from vertices in the face and body meshes to the corresponding verts in the merged
	// mesh.
	//
	// This allows the merged mesh to be quickly updated if the face or body has been changed in a
	// way that doesn't affect topology, e.g. only vertex positions or normals have changed.
	struct FMergedMeshMapping
	{
		void InitFromBodyFaceLODPairing(TConstArrayView<TPair<int32, int32>> BodyFaceLODPairing, int32 NumOriginalBodyLODs, int32 NumOriginalFaceLODs);

		struct FLODVertexMap
		{
			bool IsValid() const
			{
				return MergedMeshLODIndex != INDEX_NONE;
			}

			// For each vertex in the separate mesh (e.g. the face or body by itself), this 
			// contains the ID of the vertex in the merged mesh
			TArray<FVertexID> OriginalMeshToMergedMesh;

			int32 MergedMeshLODIndex = INDEX_NONE;
		};

		struct FVertexMap
		{
			TArray<FLODVertexMap> OriginalLODVertexMap;
		};

		FVertexMap BodyVertexMap;
		FVertexMap FaceVertexMap;
	};
}

struct FMetaHumanCharacterEditorBuild
{
	/**
	 * For a given MetaHumanCharacter assembles the MetaHuman Blueprint along with other
	 * MetaHuman assets (palette and instance).
	 * @param InMetaHumanCharacter Character that we want to build
	 * @param InParams Parameters that control the build process
	 */
	METAHUMANCHARACTEREDITOR_API static void BuildMetaHumanCharacter(
		TNotNull<class UMetaHumanCharacter*> InMetaHumanCharacter,
		const FMetaHumanCharacterEditorBuildParameters& InParams);

	/**
	 * Remove LODs from a Skeletal Mesh and DNA if one is attached
	 *
	 * @param InLODsToKeep Which LODs to keep in the mesh. If InLODsToKeep is empty, any of the LOD indices in is invalid or if there are more values
	 *					   than the number of LODs in the skeletal mesh, this function does nothing.
	 */
	METAHUMANCHARACTEREDITOR_API static void StripLODsFromMesh(TNotNull<class USkeletalMesh*> InSkeletalMesh, const TArray<int32>& InLODsToKeep);

	/**
	 * Downsize a texture it is larger than the target resolution
	 */
	METAHUMANCHARACTEREDITOR_API static void DownsizeTexture(TNotNull<class UTexture*> InTexture, int32 InTargetResolution, TNotNull<const ITargetPlatform*> InTargetPlatform);

	/**
	 * Merges body and face skeletal meshes. Resulting mesh will have only joints from the
	 * body and skin weights from the face will be transferred to the body joints.
	 * 
	 * Resulting mesh will be standalone asset.
	 */
	METAHUMANCHARACTEREDITOR_API static USkeletalMesh* MergeHeadAndBody_CreateAsset(
		TNotNull<class USkeletalMesh*> InFaceMesh,
		TNotNull<class USkeletalMesh*> InBodyMesh,
		const FString& InAssetPathAndName,
		ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All,
		UE::MetaHuman::FMergedMeshMapping* OutMeshMapping = nullptr);

	/**
	 * Merges body and face skeletal meshes. Resulting mesh will have only joints from the
	 * body and skin weights from the face will be transferred to the body joints.
	 * 
	 * Resulting mesh will be transient object on the given outer.
	 */
	METAHUMANCHARACTEREDITOR_API static USkeletalMesh* MergeHeadAndBody_CreateTransient(
		TNotNull<USkeletalMesh*> InFaceMesh,
		TNotNull<USkeletalMesh*> InBodyMesh,
		UObject* InOuter,
		ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All,
		UE::MetaHuman::FMergedMeshMapping* OutMeshMapping = nullptr);

	/**
	 * @brief Gets the default pipeline class for the given pipeline type and quality.
	 * 
	 * Default pipeline classes are defined in UMetaHumanCharacterPaletteProjectSettings
	 * 
	 * @param InPipelineType the type of pipeline to search for
	 * @param InQualityLevel the quality level for the given pipeline type to search for
	 * @return UMetaHumanCollectionPipeline class or null if no class found
	 */
	static TSoftClassPtr<UMetaHumanCollectionPipeline> GetDefaultPipelineClass(EMetaHumanDefaultPipelineType InPipelineType, EMetaHumanQualityLevel InQualityLevel);

	/**
	 * Helper to report errors to the message log of the MetaHuman editor module.
	 */
	static void ReportMessageLogErrors(
		bool bWasSuccessful,
		const FText& InSuccessMessageText,
		const FText& FailureMessageText);

	/**
	 * Duplicates the dependency objects to input root path and resolves any references as needed.
	 * If a dependency object already exists in the root folder then it is not duplicated.
	 * 
	 * @param InDependencies set of all dependency objects to be duplicated
	 * @param InDependencyRootPath new root folder for the dependencies to be copied
	 * @param InOutObjectsToReplaceWithin set with all objects that reference the dependencies and need to resolve their references
	 * @param OutDuplicatedDependencies map for every dependency to its new referenced object
	 * @param InIsAssetSupported Callable to check if an asset should be duplicated
	 */
	METAHUMANCHARACTEREDITOR_API static void DuplicateDepedenciesToNewRoot(
		const TSet<UObject*>& InDependencies,
		const FString& InDependencyRootPath,
		TSet<UObject*>& InOutObjectsToReplaceWithin,
		TMap<UObject*, UObject*>& OutDuplicatedDependencies,
		TFunction<bool(const UObject*)> InIsAssetSupported);

	/**
	 * Helper that deletes and MIDs & MICs that are children of the input MI
	 */
	METAHUMANCHARACTEREDITOR_API static void DeleteMaterialInstanceChildren(TNotNull<UMaterialInterface*> InParentMI);

	/**
	 * Finds all the outer objects that are dependencies of the input root objects by walking recursively over all referenced objects
	 * It limits the tracking to the MetaHuman Character plugin and Game mount point by default
	 * Note that dependencies do not have to be saved on disk
	 * 
	 * @param InRootObjects array of objects to look for their dependencies
	 * @param InAllowedMountPoints additional mount points that are allowed in tracking the references (on top for MHC and Game) 
	 * @param OutDependencies set with all dependency objects
	*/
	METAHUMANCHARACTEREDITOR_API static void CollectDependencies(const TArray<UObject*>& InRootObjects, const TSet<FString>& InAllowedMountPoints, TSet<UObject*>& OutDependencies);

	/**
	 * Populates an array with all the objects referenced by the input instanced struct
	 * 
	 * @param StructType UStruct instance type
	 * @param StructPtr pointer to UStruct data 
	 * @param OutObjects array of UObjects that are referenced by the struct
	*/
	METAHUMANCHARACTEREDITOR_API static void CollectUObjectReferencesFromStruct(const UStruct* StructType, const void* StructPtr, TArray<UObject*>& OutObjects);

	/**
	 * Helper returning the version stored in the metadata. Version of 0.0 is returned if metadata is not found
	 */
	METAHUMANCHARACTEREDITOR_API static UE::MetaHuman::FMetaHumanAssetVersion GetMetaHumanAssetVersion(TNotNull<const UObject*> InAsset);

	UE_DEPRECATED(5.7, "Please use GetCurrentMetaHumanAssetVersion") 
	METAHUMANCHARACTEREDITOR_API static UE::MetaHuman::FMetaHumanAssetVersion GetMetaHumanAssetVersion();

	/**
	 * Helper returning the Actor BP version used in the plugin as the current engine version
	 */
	METAHUMANCHARACTEREDITOR_API static UE::MetaHuman::FMetaHumanAssetVersion GetCurrentMetaHumanAssetVersion();

	/**
	 * Helper returning the earliest compatible version of the engine for MetaHuman Character
	 */
	METAHUMANCHARACTEREDITOR_API static UE::MetaHuman::FMetaHumanAssetVersion GetFirstMetaHumanCompatibleVersion();

	/**
	 * Helper to check if provided asset has matching or higher version in metadata.
	 */
	METAHUMANCHARACTEREDITOR_API static bool MetaHumanAssetMetadataVersionIsCompatible(TNotNull<const UObject*> InAsset);

	/**
	 * Helper to set the latest MetaHuman Asset Version used in the plugin
	 */
	METAHUMANCHARACTEREDITOR_API static void SetMetaHumanVersionMetadata(TNotNull<UObject*> InObject);

};