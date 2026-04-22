// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlRig.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCollectionEditorPipeline.h"

#include "Dataflow/DataflowObject.h"
#include "Misc/NotNull.h"
#include "Templates/SubclassOf.h"
#include "TextureGraph.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakObjectPtrFwd.h"
#include "Engine/DataAsset.h"
#include "Engine/SkinnedAssetCommon.h"
#include "EditorUtilityObject.h"

#include "MetaHumanDefaultEditorPipelineBase.generated.h"

class AActor;
struct FMetaHumanCharacterPreviewAssets;
struct FMetaHumanCharacterGeneratedAssets;
class ITargetPlatform;
class UControlRigBlueprint;
class UMetaHumanCharacterEditorPipelineSpecification;
class UMetaHumanCharacterInstance;
class UTexture2D;

namespace UE::MetaHuman
{
	struct FMetaHumanAssetVersion;
}

USTRUCT()
struct FMetaHumanInputTextureProperties
{
	GENERATED_BODY()

public:
	/** The name of the material slot on the face mesh that this material is set on */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName SourceMaterialSlotName;

	/** The name of the material parameter that this texture is set on, on the source material */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName SourceMaterialParameterName;

	/** The name of the input parameter for this texture in the Texture Graph */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName InputParameterName;
};

USTRUCT()
struct FMetaHumanInputMaterialProperties
{
	GENERATED_BODY()

public:
	/** The name of the material slot on the face mesh that this material is set on */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName SourceMaterialSlotName;

	/** The name of the input parameter for this material in the Texture Graph */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName InputParameterName;

	/** 
	 * If the source material slot is part of a group of slots with one for each LOD, set this to
	 * the best LOD index that the source material is used on.
	 * 
	 * Where supported, this will be used to skip baking materials for LODs when the baked material
	 * would be the same as a better LOD.
	 */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	int32 MainSectionTopLODIndex = INDEX_NONE;
};

USTRUCT()
struct FMetaHumanOutputTextureProperties
{
	GENERATED_BODY()

	/** The name of the output parameter in the Texture Graph Instance's Export Settings */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName OutputTextureNameInGraph;

	/** The relative path to the folder where the output texture should be written */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FString OutputTextureFolder;

	/** The name that the output texture should be given. Leave as None to use the texture name from the Texture Graph Instance. */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName OutputTextureName;

	/** Indicates this will output a virtual texture if support is enabled */
	UPROPERTY(EditAnywhere, Category = "Texture Baking" )
	bool bOutputsVirtualTexture = false;

	/** Name of the texture asset if virtual texture support is enabled */
	UPROPERTY(EditAnywhere, Category = "Texture Baking", meta = (EditCondition = "bOutputsVirtualTexture", EditConditionHides))
	FName OutputVirtualTextureName;

	/** Names of the material slots where this texture will be set */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FName> OutputMaterialSlotNames;

	/** The name of the material parameter that this texture should be set on on the output material */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName OutputMaterialParameterName;

	/** The name of the material parameter that this texture should be set if Virtual Texture its a Virtual Texture */
	UPROPERTY(EditAnywhere, Category = "Texture Baking", meta = (EditCondition = "bOutputsVirtualTexture", EditConditionHides))
	FName OutputMaterialParameterNameVT;
};

USTRUCT()
struct FMetaHumanTextureGraphOutputProperties
{
	GENERATED_BODY()

public:
	/** The Texture Graph Instance to use as a template for this bake */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TObjectPtr<UTextureGraphInstance> TextureGraphInstance;

	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TMap<FName, float> InputValues;

	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FMetaHumanInputMaterialProperties> InputMaterials;

	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FMetaHumanOutputTextureProperties> OutputTextures;
};

USTRUCT()
struct FMetaHumanBakedMaterialProperties
{
	GENERATED_BODY()

public:
	/** The name of the main material slot on the baked mesh that this material should be set on */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName PrimaryMaterialSlotName;

	/** The material to use as the parent for the generated material instance */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TObjectPtr<UMaterialInterface> Material;
	
	/** Indicates this may output a material with Virtual Texture support if enabled */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	bool bOutputsMaterialWithVirtualTextures = false;
	
	/** The material to use as parent of for the generated material instance if Virtual Texture support is enabled */
	UPROPERTY(EditAnywhere, Category = "Texture Baking", meta = (EditCondition = "bOutputsMaterialWithVirtualTextures", EditConditionHides))
	TObjectPtr<UMaterialInterface> MaterialVT;

	/** The name of any other material slots on the baked mesh that this material should be set on */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FName> AdditionalMaterialSlotNames;

	/** The relative path to the folder where the generated material instance should be written */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FString OutputMaterialFolder;

	/** The name that the generated material instance should be given */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	FName OutputMaterialName;

	/** The name that the generated material instance should be given if Virtual Texture support is enabled */
	UPROPERTY(EditAnywhere, Category = "Texture Baking", meta = (EditCondition = "bOutputsMaterialWithVirtualTextures", EditConditionHides))
	FName OutputMaterialNameVT;

	/** List of parameters to copy from the input material defined in PrimaryMaterialSlotName */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FName> ParametersToCopy;
};

UENUM()
enum class EMetaHumanBuildTextureResolution : int32
{
	Res256 = 256 UMETA(DisplayName = "256"),
	Res512 = 512 UMETA(DisplayName = "512"),
	Res1024 = 1024 UMETA(DisplayName = "1k"),
	Res2048 = 2048 UMETA(DisplayName = "2k"),
	Res4096 = 4096 UMETA(DisplayName = "4k"),
	Res8192 = 8192 UMETA(DisplayName = "8k"),
};

USTRUCT()
struct FMetaHumanHairProperties
{
	GENERATED_BODY()

public:
	/** The material slots on the face mesh to set the follicle map on */
	UPROPERTY(EditAnywhere, Category = "Hair")
	TArray<FName> FollicleMapMaterialSlotNames;

	/** The material parameter to set the follicle map on */
	UPROPERTY(EditAnywhere, Category = "Hair")
	FName FollicleMapMaterialParameterName;

	/** 
	 * The material parameter to set to true when a follicle map should be used.
	 * 
	 * Will be set to false when there is no follicle map.
	 * 
	 * If this parameter isn't required, leave this property as None.
	 */
	UPROPERTY(EditAnywhere, Category = "Hair")
	FName UseFollicleMapMaterialParameterName;

	/** Size of the root in the follicle mask (in pixels) */
	UPROPERTY(EditAnywhere, Category = "Hair")
	int32 FollicleMapRootRadius = 8;

	/** The resolution that the follicle map should be generated at */
	UPROPERTY(EditAnywhere, Category = "Hair")
	EMetaHumanBuildTextureResolution FollicleMapResolution = EMetaHumanBuildTextureResolution::Res4096;

	/** 
	 * Pre-Baked Grooms are an optimization that reduces the number of texture samplers needed in
	 * the skin material. Read on for more information.
	 * 
	 * Grooms with short hair are often baked into the skin material, especially at low LODs, to 
	 * save the cost of having separate Groom Components for them.
	 * 
	 * In order to support multiple grooms at once (beard, eyebrows, etc), the skin material needs
	 * to sample from multiple textures. This can cause it to go beyond the maximum number of 
	 * texture samplers, if other features are enabled that also sample from many textures.
	 * 
	 * The Pre-Baked Grooms feature bakes multiple grooms into a set of two textures, which the 
	 * skin material then samples from. This allows several grooms to be baked into the skin 
	 * for the cost of two texture samplers.
	 */
	UPROPERTY(EditAnywhere, Category = "Pre-Baked Grooms")
	bool bUsePreBakedGrooms = false;

	/** 
	 * The material to use for generating the pre-baked groom textures.
	 * 
	 * Must have the same parameters as the skin material for baked grooms.
	 */
	UPROPERTY(EditAnywhere, Category = "Pre-Baked Grooms", meta = (EditCondition = "bUsePreBakedGrooms"))
	TObjectPtr<UMaterialInterface> PreBakedGroomGeneratorMaterial;

	/** 
	 * A Texture Graph Instance that maps the output of the generator material to the pre-baked
	 * groom textures.
	 */
	UPROPERTY(EditAnywhere, Category = "Pre-Baked Grooms", meta = (EditCondition = "bUsePreBakedGrooms"))
	TObjectPtr<UTextureGraphInstance> PreBakedGroomTextureGraphInstance;

	/** The name of the material node in the Texture Graph */
	UPROPERTY(EditAnywhere, Category = "Pre-Baked Grooms", meta = (EditCondition = "bUsePreBakedGrooms"))
	FName PreBakedGroomTextureGraphInputName;

	/** 
	 * Setup for the generated pre-baked groom textures.
	 * 
	 * Note that there are some restrictions on this for pre-baked groom textures specifically:
	 * 
	 * 1. Only non-virtual textures are supported
	 * 
	 * 2. OutputTextureName must be set to a valid, unique name
	 * 
	 * Also note that OutputMaterialSlotNames is ignored, as the setup for this is generated procedurally.
	 */
	UPROPERTY(EditAnywhere, Category = "Pre-Baked Grooms", meta = (EditCondition = "bUsePreBakedGrooms"))
	TArray<FMetaHumanOutputTextureProperties> PreBakedGroomTextureGraphOutputTextures;
};

USTRUCT()
struct FMetaHumanCostumeProperties
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Costume")
	TObjectPtr<UDataflow> OutfitResizeDataflowAsset;
};

USTRUCT()
struct FMetaHumanBodyRigLogicProperties
{
	GENERATED_BODY()
public:
	
	/**The Control Rig to use to unpack Swing/Twist and Half Rotation solvers to. If none is specified a new one will be created.*/
	UPROPERTY(EditAnywhere, Category = "Body")
	TObjectPtr<UControlRigBlueprint> ControlRig;

	/**Unpack the RBF Solvers to PoseAssets and AnimSequences. If PostProcessAnimBp is set, PoseDriver nodes will also be created inside it.*/
	UPROPERTY(EditAnywhere, Category = "Body RigLogic")
	bool bUnpackRbfToPoseAssets = false;

	/**Unpack the finger half rotation RBF solvers to Control Rig for improved performance.*/
	UPROPERTY(EditAnywhere, Category = "Body RigLogic", meta = (EditCondition = "bUnpackRbfToPoseAssets"))
	bool bUnpackFingerHalfRotationsToControlRig = false;

	/**Unpack the Swing/Twist setup to Control Rig.*/
	UPROPERTY(EditAnywhere, Category = "Body RigLogic")
	bool bUnpackSwingTwistToControlRig = false;
};

USTRUCT()
struct FMetaHumanBodyProperties
{
	GENERATED_BODY()

public:
	/**Override the PostProcess AnimBlueprint on the newly created body SkeletalMesh. If UnpackRigLogic is enabled, it will also be used to unpack the
	 *RBF Solvers and Control Rig to. If none is specified when unpacking is enabled only PoseAssets will be created.*/
	UPROPERTY(EditAnywhere, Category = "Body")
	class TSoftClassPtr<UAnimInstance> PostProcessAnimBp;

	UPROPERTY(EditAnywhere, Category = "Body")
	bool bUnpackRigLogic = false;

	UPROPERTY(EditAnywhere, Category = "Body", meta = (EditCondition = "bUnpackRigLogic", EditConditionHides))
	FMetaHumanBodyRigLogicProperties BodyRigLogicUnpackProperties;
};


/**
 * Configures the MetaHuman LODs that the pipeline uses to build a character
 */
USTRUCT()
struct FMetaHumanLODProperties
{
	GENERATED_BODY()

	// Which LODs of the Face are going to be in the built MetaHuman. If empty all LODs are going to be exported
	UPROPERTY(EditAnywhere, Category = "Face")
	TArray<int32> FaceLODs;

	// Which LODs of the Body are going to be in the built MetaHuman. If empty all LODs are going to be exported
	UPROPERTY(EditAnywhere, Category = "Body")
	TArray<int32> BodyLODs;

	// Whether or not to override the Face Skeletal Mesh LOD Settings
	UPROPERTY(EditAnywhere, Category = "LOD Settings", meta = (InlineEditConditionToggle))
	bool bOverrideFaceLODSettings = false;

	// Whether or not to override the Body Skeletal Mesh LOD Settings
	UPROPERTY(EditAnywhere, Category = "LOD Settings", meta = (InlineEditConditionToggle))
	bool bOverrideBodyLODSettings = false;

	// LOD Settings asset to set to the exported Face Mesh
	UPROPERTY(EditAnywhere, Category = "LOD Settings", meta = (EditCondition = "bOverrideFaceLODSettings"))
	TSoftObjectPtr<class USkeletalMeshLODSettings> FaceLODSettings;

	// LOD Settings asset to set to the exported Body Mesh
	UPROPERTY(EditAnywhere, Category = "LOD Settings", meta = (EditCondition = "bOverrideBodyLODSettings"))
	TSoftObjectPtr<class USkeletalMeshLODSettings> BodyLODSettings;
};

/**
 * Base class for an Editor Utility Object that is capable of baking the Normals
 * of a skeletal mesh into a texture
 */
UCLASS(Abstract, Blueprintable)
class ULODBakingUtility : public UEditorUtilityObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintImplementableEvent)
	UPARAM(DisplayName = "Baked Texture")
	TArray<UTexture2D*> BakeTangentNormals(USkeletalMesh* InTarget, class UGeometryScriptDebug* InDebug);
};

UCLASS()
class UMetaHumanMaterialBakingSettings : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The Texture Graphs to use for baking */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FMetaHumanTextureGraphOutputProperties> TextureGraphs;

	/** The output materials that the baked textures should be set on */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	TArray<FMetaHumanBakedMaterialProperties> BakedMaterials;

	/** The class responsible for baking the normals for use in higher LODs */
	UPROPERTY(EditAnywhere, Category = "LOD Baking")
	TSubclassOf<ULODBakingUtility> LODBakingUtilityClass;

	/**
	 * If this is enabled, the Texture Graph Instances and source textures for the bake will be
	 * generated as assets so that the user can inspect them and re-run the bake if they wish.
	 */
	UPROPERTY(EditAnywhere, Category = "Texture Baking")
	bool bGenerateTextureGraphInstanceAssets = false;
};

/**
 * Options to configure how the pipeline should bake textures
 */
USTRUCT()
struct FMetaHumanMaterialBakingOptions
{
	GENERATED_BODY()

	// A settings object containing the texture graphs to be executed and their outputs
	UPROPERTY(EditAnywhere, Category = "Baking")
	TSoftObjectPtr<UMetaHumanMaterialBakingSettings> BakingSettings;

	// Overrides for the output texture resolutions
	//
	// The key is the OutputTextureName
	UPROPERTY(EditAnywhere, Category = "Baking")
	TMap<FName, EMetaHumanBuildTextureResolution> TextureResolutionsOverrides;
	
	/**
	 * Refreshes the list of resolution overrides based on the output textures defined in
	 * Baking Settings. This action removes entries that are not defined in Baking Settings
	 * and adds any new ones found
	 */
	void RefreshTextureResolutionsOverrides();
};

/**
 * Options to configure the resolutions of synthesized textures
 */
USTRUCT()
struct FMetaHumanBuildTextureProperties
{
	GENERATED_BODY()

	// Set the override resolutions of the synthesized face textures
	UPROPERTY(EditAnywhere, Category = "Face")
	TMap<EFaceTextureType, EMetaHumanBuildTextureResolution> Face;
};

/**
 * Common base class for editor pipelines of UMetaHumanDefaultPipelineBase
 */
UCLASS(Abstract)
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanDefaultEditorPipelineBase : public UMetaHumanCollectionEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanDefaultEditorPipelineBase();

	virtual void BuildCollection(
		TNotNull<const UMetaHumanCollection*> Collection,
		TNotNull<UObject*> OuterForGeneratedAssets,
		const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
		const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
		const FInstancedStruct& BuildInput,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		const FOnBuildComplete& OnComplete) const override;

	virtual bool CanBuild() const override;

	virtual void UnpackCollectionAssets(
		TNotNull<UMetaHumanCollection*> Collection,
		FMetaHumanCollectionBuiltData& CollectionBuiltData,
		const FOnUnpackComplete& OnComplete) const override;

	virtual bool TryUnpackInstanceAssets(
		TNotNull<UMetaHumanCharacterInstance*> Instance,
		FInstancedStruct& AssemblyOutput,
		TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
		const FString& TargetFolder) const override;

	virtual bool IsWardrobeItemCompatibleWithSlot(FName InSlotName, TNotNull<const UMetaHumanWardrobeItem*> InWardrobeItem) const override;

	virtual void SetValidationContext(TScriptInterface<class IMetaHumanValidationContext> InValidationContext) override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;
	virtual TSubclassOf<AActor> GetEditorActorClass() const override;

	UPROPERTY(EditAnywhere, Category = "Pipeline", meta = (MustImplement = "/Script/MetaHumanCharacterPaletteEditor.MetaHumanCharacterEditorActorInterface"))
	TSubclassOf<AActor> EditorActorClass;

	/** Which Face Skeleton to use.*/
	UPROPERTY(EditAnywhere, Category = "Dependencies|Skeletons", meta = (PipelineDisplay = "Advanced"))
	TSoftObjectPtr<class USkeleton> FaceSkeleton;

	/** Which Body Skeleton to use.*/
	UPROPERTY(EditAnywhere, Category = "Dependencies|Skeletons", meta = (PipelineDisplay = "Advanced"))
	TSoftObjectPtr<class USkeleton> BodySkeleton;

	/**
	 * If enabled, the materials used in editor will be baked down to textures, so that the built
	 * MetaHuman can use simplified materials that are faster to render.
	 */
	UPROPERTY(EditAnywhere, Category = "Materials")
	bool bBakeMaterials;

	// Configure the material face baking options for this pipeline
	UPROPERTY(EditAnywhere, Category = "Materials", meta = (EditCondition = "bBakeMaterials", EditConditionHides, PipelineDisplay = "Textures"))
	FMetaHumanMaterialBakingOptions FaceMaterialBakingOptions;

	// Configure the material body baking options for this pipeline
	UPROPERTY(EditAnywhere, Category = "Materials", meta = (EditCondition = "bBakeMaterials", EditConditionHides, PipelineDisplay = "Textures"))
	FMetaHumanMaterialBakingOptions BodyMaterialBakingOptions;

	// Overrides for the output texture resolutions defined in the face baking options
	UE_DEPRECATED(5.7, "This property has no effect in the pipeline. Please use FaceMaterialBakingOptions.TextureResolutionsOverrides")
	UPROPERTY(EditAnywhere, Category = "Materials", meta = (EditCondition = "bBakeMaterials", EditConditionHides))
	TMap<FName, EMetaHumanBuildTextureResolution> FaceBakedTextureResolutions;

	// Overrides for the output texture resolutions defined in the body baking options
	UE_DEPRECATED(5.7, "This property has no effect in the pipeline. Please use BodyMaterialBakingOptions.TextureResolutionsOverrides")
	UPROPERTY(EditAnywhere, Category = "Materials", meta = (EditCondition = "bBakeMaterials", EditConditionHides))
	TMap<FName, EMetaHumanBuildTextureResolution> BodyBakedTextureResolutions;

	// Configure the maximum resolution for each of the source textures when building the character
	UPROPERTY(EditAnywhere, Category = "Textures")
	FMetaHumanBuildTextureProperties MaxTextureResolutions;

	// Configure the LODs of the character being built
	UPROPERTY(EditAnywhere, Category = "LODs")
	FMetaHumanLODProperties LODProperties;

	UPROPERTY(EditAnywhere, Category = "Hair")
	FMetaHumanHairProperties HairProperties;

	UPROPERTY(EditAnywhere, Category = "Costume")
	FMetaHumanCostumeProperties CostumeProperties;

	UPROPERTY(EditAnywhere, Category = "Body", meta=(PipelineDisplay = "Advanced"))
	FMetaHumanBodyProperties BodyProperties;

private:
	void OnCharacterPaletteAssetsUnpacked(
		EMetaHumanBuildStatus Result,
		TWeakObjectPtr<class UMetaHumanCollection> Palette,
		TNotNull<FMetaHumanCollectionBuiltData*> CollectionBuiltData,
		FOnUnpackComplete OnComplete) const;

	bool TryUnpackObject(UObject* Object, UObject* UnpackingAsset, FString& InOutAssetPath, TSet<FString>& OutUnpackedAssetPaths) const;
	bool TryMoveObjectToAssetPackage(UObject* Object, FStringView NewAssetPath) const;

	static void ReplaceReferencesInAssemblyOutput(FInstancedStruct& AssemblyOutput, TNotNull<const UObject*> OriginalObject, TNotNull<UObject*> ReplacementObject);

	/**
	 * Bakes materials to textures with the provided settings.
	 * 
	 * If bCreateInstancesOfBakedMaterials is true, the materials specified in 
	 * FMetaHumanMaterialBakingOptions to be used with the baked textures will be instanced first,
	 * so that the original materials aren't modified. This setting should be used when the
	 * FMetaHumanMaterialBakingOptions come from an asset on disk.
	 * 
	 * If bCreateInstancesOfBakedMaterials is false, the baked materials will be modified and used
	 * directly. This can be used when FMetaHumanMaterialBakingOptions has been generated 
	 * procedurally and set up with baked materials for this purpose.
	 */
	bool TryBakeMaterials(
		const FString& BaseOutputFolder,
		const FMetaHumanMaterialBakingOptions& InMaterialBakingOptions,
		TArray<FSkeletalMaterial>& InOutSkelMeshMaterials,
		const TMap<FName, TObjectPtr<UMaterialInterface>>& RemovedMaterialSlots,
		const TArray<int32>& MaterialChangesPerLOD,
		TNotNull<UObject*> GeneratedAssetOuter,
		FMetaHumanCharacterGeneratedAssets& InOutGeneratedAssets,
		TArray<TObjectPtr<UTexture>>& OutGeneratedTextures,
		bool bCreateInstancesOfBakedMaterials = true) const;

protected:
	/**
	 * Helper function for generating the blueprint actor asset. It will try to reuse the existing blueprint,
	 * but if it fails or there is no existing blueprint on the given path, it will generate a new one.
	 */
	UBlueprint* WriteActorBlueprintHelper(
		TSubclassOf<AActor> InBaseActorClass,
		const FString& InBlueprintPath,
		const TFunction<bool(UBlueprint*)> CanReuseBlueprintFunc,
		const TFunction<UBlueprint*(class UPackage*)> GenerateBlueprintFunc) const;

	/**
	 * Returns true if the object is an asset of this plugin by checking if the package name root
	 * matches the name of plugin this class is
	 */
	static bool IsPluginAsset(TNotNull<UObject*> InObject);

	/**
	 * Generates a skeleton for unpacking. If @p InBaseSkeleton is a plugin asset, unpack it to target common folder, otherwise use it as is
	 *
	 * @param InGeneratedAssets list of generated assets where this skeleton will be added
	 * @param InBaseSkeleton the base skeleton to use. This will be either the Face or Body skeletons selected when configuring the pipeline
	 * @param InTargetFolderName the folder name to place the skeleton on
	 * @param InOuterForGeneratedAssets the outer to use if the base skeleton needs to be duplicated
	 * @return the skeleton to be set in the target skeletal mesh, either the Face or Body
	 */
	virtual TNotNull<USkeleton*> GenerateSkeleton(FMetaHumanCharacterGeneratedAssets& InGeneratedAssets,
												  TNotNull<USkeleton*> InBaseSkeleton,
												  const FString& InTargetFolderName,
												  TNotNull<UObject*> InOuterForGeneratedAssets) const;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;

	UPROPERTY(Transient)
	TScriptInterface<class IMetaHumanValidationContext> ValidationContext;

private:

	/**
	 * Remove LODs if specified by the pipeline
	 */
	void RemoveLODsIfNeeded(FMetaHumanCharacterGeneratedAssets& InGeneratedAssets, TMap<FName, TObjectPtr<UMaterialInterface>>& OutRemovedMaterialSlots) const;

	void ProcessGroomAndClothSlots(
		TNotNull<const UMetaHumanCollection*> CharacterCollection,
		FMetaHumanCollectionBuiltData& BuiltData,
		const TArray<FMetaHumanPinnedSlotSelection>& SortedPinnedSlotSelections,
		const TArray<FMetaHumanPaletteItemPath>& SortedItemsToExclude,
		EMetaHumanCharacterPaletteBuildQuality Quality,
		ITargetPlatform* TargetPlatform,
		TNotNull<UObject*> OuterForGeneratedAssets,
		const FString& TextureOutputFolder,
		TMap<FMetaHumanPaletteItemKey, struct FCharacterPipelineData>& CharacterPipelineData) const;

	bool ProcessBakedMaterials(
		const FString& TextureOutputFolder, 
		FMetaHumanCollectionBuiltData& BuiltData,
		FMetaHumanCharacterGeneratedAssets& GeneratedAssets,
		TNotNull<UObject*> OuterForGeneratedAssets, 
		FCharacterPipelineData& PipelineData) const;

	bool CanResizeOutfits() const;

	bool ConfirmUpdateCommonAssetsForBP(TNotNull<const UObject*> InGeneratedBP, const FString& InAssetPath) const;
};

// A simple wrapper to allow an FInstancedStruct to be visible to the Garbage Collector
UCLASS()
class UMetaHumanStructHost : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FInstancedStruct Struct;
};
