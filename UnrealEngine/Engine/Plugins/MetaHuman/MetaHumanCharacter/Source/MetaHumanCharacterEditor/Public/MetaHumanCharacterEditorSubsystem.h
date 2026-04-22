// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "Misc/EnumRange.h"
#include "Misc/NotNull.h"
#include "TickableEditorObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakInterfacePtr.h"
#include "SkelMeshDNAUtils.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanFaceTextureSynthesizer.h"
#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "Subsystem/MetaHumanCharacterService.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"
#include "Misc/Change.h"

#include "MetaHumanCharacterEditorSubsystem.generated.h"


struct FMetaHumanCharacterGeneratedAssets;
struct FMetaHumanCharacterGeneratedAssetOptions;
struct FMetaHumanRigEvaluatedState;
enum class EMetaHumanServiceRequestResult;
enum class EMetaHumanClothingVisibilityState : uint8;
class FMetaHumanFaceTextureAttributeMap;

UENUM()
enum class EImportErrorCode : uint8
{
	FittingError,
	InvalidInputData,
	InvalidInputBones,
	InvalidHeadMesh,
	InvalidLeftEyeMesh,
	InvalidRightEyeMesh,
	InvalidTeethMesh,
	NoHeadMeshPresent,
	NoEyeMeshesPresent,
	NoTeethMeshPresent,
	IdentityNotConformed,
	GeneralError,
	CombinedBodyCannotBeImportedAsWholeRig,
	Success
};

class FRemoveRigCommandChange : public FCommandChange
{
public:

	FRemoveRigCommandChange(
		const TArray<uint8>& InOldDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
		const TArray<uint8>& InOldBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter);

	//~Begin FCommandChange interface
	virtual FString ToString() const override
	{
		return FString(TEXT("Remove Rig"));
	}

	virtual void Apply(UObject* InObject) override
	{
		ApplyChange(InObject, NewDNABuffer, NewState, NewBodyDNABuffer, NewBodyState);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyChange(InObject, OldDNABuffer, OldState, OldBodyDNABuffer, OldBodyState);
	}

	virtual bool HasExpired(UObject* InObject) const override;

	//~End FCommandChange interface	

protected:

	void ApplyChange(UObject* InObject,
		const TArray<uint8>& InDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
		const TArray<uint8>& InBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);

	TArray<uint8> OldDNABuffer;
	TArray<uint8> NewDNABuffer;

	TArray<uint8> OldBodyDNABuffer;
	TArray<uint8> NewBodyDNABuffer;

	TSharedRef<const FMetaHumanCharacterIdentity::FState> OldState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> NewState;

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldBodyState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewBodyState;

	bool bFixedBodyType = false;
};

// a specialization of the above with identical functionality but a different name so it appears correctly in the undo stack
class FAutoRigCommandChange : public FRemoveRigCommandChange
{
public:

	FAutoRigCommandChange(
		const TArray<uint8>& InOldDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
		const TArray<uint8>& InOldBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter);

	//~Begin FCommandChange interface
	virtual FString ToString() const override
	{
		return FString(TEXT("Apply Auto-rig"));
	}
};

// TODO: Merge this with the enum defined in MetaHumanARServiceRequest.h
UENUM()
enum class EMetaHumanRigType : uint8
{
	JointsOnly,
	JointsAndBlendShapes
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTextureRequestParams
{
	GENERATED_BODY()

	// Weather or not to report the progress of the request in the form of notification items
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Synthesis")
	bool bReportProgress = true;

	// Set to true to make it a blocking request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Synthesis")
	bool bBlocking = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAutoRiggingRequestParams
{
	GENERATED_BODY()
	
	// The type of rig to request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	EMetaHumanRigType RigType = EMetaHumanRigType::JointsOnly;

	// Weather or not to report the progress of the request in the form of notification items
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	bool bReportProgress = true;

	// Set to true to make it a blocking request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	bool bBlocking = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFitToVerticesParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	FFitToTargetOptions Options;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> HeadVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> LeftEyeVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> RightEyeVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> TeethVertices;
};

namespace UE::MetaHuman
{
	enum class ERigType;
}

DECLARE_DELEGATE(FOnNotifyLightingEnvironmentChanged)
DECLARE_DELEGATE_OneParam(FOnStudioLightRotationChanged, float InRotation);
DECLARE_DELEGATE_OneParam(FOnStudioBackgroundColorChanged, const FLinearColor& InBackgroundColor)

/**
 * Helper struct used to hold a data needed for each character being edited
 */
USTRUCT()
struct FMetaHumanCharacterEditorData
{
	GENERATED_BODY()

	FMetaHumanCharacterEditorData(
		TNotNull<USkeletalMesh*> InFaceMesh,
		TNotNull<USkeletalMesh*> InBodyMesh,
		TSharedRef<class FDNAToSkelMeshMap> InFaceDnaToSkelMeshMap,
		TSharedRef<class FDNAToSkelMeshMap> InBodyDnaToSkelMeshMap,
		TSharedRef<FMetaHumanCharacterIdentity::FState> InFaceState,
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InBodyState)
	: FaceMesh(InFaceMesh)
	, BodyMesh(InBodyMesh)
	, FaceDnaToSkelMeshMap(InFaceDnaToSkelMeshMap)
	, BodyDnaToSkelMeshMap(InBodyDnaToSkelMeshMap)
	, FaceState(InFaceState)
	, BodyState(InBodyState)
	{
	}

	// DO NOT USE.
	// For Unreal internals only. Default-constructed instances are not considered valid.
	FMetaHumanCharacterEditorData();

	// List of editor actors for a particular characters
	TArray<TWeakInterfacePtr<class IMetaHumanCharacterEditorActorInterface>> CharacterActorList;

	// Image objects used as temp storage for the texture synthesis output
	TMap<EFaceTextureType, FImage> CachedSynthesizedImages;
	
	// Temporary storage for HF albedo maps returned by the service, used for local texture synthesis
	TStaticArray<TArray<uint8>, 4> CachedHFAlbedoMaps;

	// Maps of futures used to do async loading of texture data
	TSortedMap<EFaceTextureType, TSharedFuture<FSharedBuffer>> SynthesizedFaceTexturesFutures;
	TSortedMap<EBodyTextureType, TSharedFuture<FSharedBuffer>> HighResBodyTexturesFutures;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	/** Invisible actor driving the preview actor. */
	UPROPERTY()
	TObjectPtr<class AMetaHumanInvisibleDrivingActor> InvisibleDrivingActor;

	// All members of this will be UMaterialInstanceDynamics, so it's safe to cast them
	UPROPERTY()
	FMetaHumanCharacterFaceMaterialSet HeadMaterials;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;
	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;

	// Temporary textures used to combine multiple hidden face maps into one for the character preview
	UPROPERTY()
	TObjectPtr<UTexture2D> TempCombinedHeadHiddenFaceMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> TempCombinedBodyHiddenFaceMap;

	UPROPERTY()
	bool bClothingVisible = true;

	// The latest skin settings to be used for generating textures and setting material parameters
	TOptional<FMetaHumanCharacterSkinSettings> SkinSettings;

	// The latest eyes settings to be used when updating the preview materials
	TOptional<FMetaHumanCharacterEyesSettings> EyesSettings;

	// The latest makeup settings to be used when updating the preview materials
	TOptional<FMetaHumanCharacterMakeupSettings> MakeupSettings;

	// The latest face evaluation settings which include vertex delta scale
	TOptional<FMetaHumanCharacterFaceEvaluationSettings> FaceEvaluationSettings;

	// The latest head model settings which include eyelashes parameters and variants.
	TOptional<FMetaHumanCharacterHeadModelSettings> HeadModelSettings;

	// Reference to the mapping between face DNA and Face Skeletal Mesh
	TSharedRef<class FDNAToSkelMeshMap> FaceDnaToSkelMeshMap;

	// Reference to the mapping between body DNA and Body Skeletal Mesh
	TSharedRef<class FDNAToSkelMeshMap> BodyDnaToSkelMeshMap;

	// Reference to the character identity creator
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState;

	// Reference to the character body identity creator
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState;

	/** 
	 * Merged head and body mesh used for fast outfit resizing while editing the face or body.
	 * 
	 * Contains only LOD 0. May have other limitations.
	 */
	UPROPERTY()
	TObjectPtr<USkeletalMesh> PreviewMergedHeadAndBody;

	// Info about how the separate face and body mesh verts correspond to the verts in the merged mesh.
	UE::MetaHuman::FMergedMeshMapping PreviewMergedMeshMapping;

	// Delegate called when the Face State changes
	FSimpleMulticastDelegate OnFaceStateChangedDelegate;

	// Delegate called when the Face State changes
	FSimpleMulticastDelegate OnBodyStateChangedDelegate;

	// Delegate used for Environment Lighting studio update.
	FOnNotifyLightingEnvironmentChanged NotifyLightingEnvironmentChangedDelegate;
	FOnStudioLightRotationChanged EnvironmentLightRotationChangedDelegate;
	FOnStudioBackgroundColorChanged EnvironmentBackgroundColorChangedDelegate;
};

/** 
 * The set of assets needed for the preview build.
 * 
 * Importantly, these assets belong to the editor subsystem and must not be modified by the preview 
 * build.
 */
USTRUCT()
struct FMetaHumanCharacterPreviewAssets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;

	UPROPERTY()
	TMap<FString, float> BodyMeasurements;
};

struct FMetaHumanCharacterPreviewAssetOptions
{
	bool bGenerateMergedHeadAndBodyMesh = false;
	bool bGenerateBodyMeasurements = false;
};

USTRUCT()
struct FImportFromIdentityParams
{
	GENERATED_BODY()
	
	// Set to true to use the eye meshes to fit when importing a MetaHuman Identity asset; if false, they are not used
	UPROPERTY(EditAnywhere, Category = "Import Identity Options")
	bool bUseEyeMeshes = true;

	// Set to true to use the teeth mesh to fit when importing a MetaHuman Identity asset; if false, it is not used
	UPROPERTY(EditAnywhere, Category = "Import Identity Options")
	bool bUseTeethMesh = true;

	// Set to true to use the metric scale of the Identity head when importing a MetaHuman Identity asset; if false, the Identity head will be scaled to MetaHuman size
	UPROPERTY(EditAnywhere, Category = "Import Identity Options")
	bool bUseMetricScale = false;
};

USTRUCT(BlueprintType)
struct FImportFromDNAParams
{
	GENERATED_BODY()

	// Set to true to import the DNA and create a fully rigged Character which cannot be edited (and any other options will be ignored); if false, the DNA will be fitted to give an editable mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import DNA Options")
	bool bImportWholeRig = true;

	// Set the alignment options to use when importing a MetaHuman DNA head asset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import DNA Options", meta = (EditCondition = "!bImportWholeRig", EditConditionHides))
	EAlignmentOptions AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
};

USTRUCT()
struct FImportBodyFromDNAParams
{
	GENERATED_BODY()

	// When enabled, imports mesh, joints, RBF, and skin weights from the DNA file, resulting in a fixed, non-editable body type. Must be body only, using MetaHuman topology. Disabling this option allows for generating a parametric body type using mesh and, optionally, skeleton from DNA.
	UPROPERTY(EditAnywhere, Category = "Import Whole Rig")
	bool bImportWholeRig = false;

	// Set the conform options to use when conforming a MetaHuman DNA body asset
	UPROPERTY(EditAnywhere, Category = "Import DNA Options", meta = (ShowOnlyInnerProperties, EditCondition = "!bImportWholeRig"))
	FConformBodyParams ConformBodyParams;
};


USTRUCT(BlueprintType)
struct FImportFromTemplateParams
{
	GENERATED_BODY()

	// Get corresponding vertices from template mesh by matching the template mesh's UVs to MH standard
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bMatchVerticesByUVs = false;

	// Set to true to use the eye meshes to fit when importing a SkelMesh; if false, they are not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bUseEyeMeshes = true;

	// Set to true to use the teeth mesh to fit when importing a SkelMesh; if false, it is not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bUseTeethMesh = true;

	// Set the alignment options to use when importing a SkelMesh or Static Mesh head asset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	EAlignmentOptions AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
};


struct FEditorDataForCharacterCreationParams
{
	/** A parameter to control if we should wait for any async tasks to complete */
	bool bBlockUntilComplete = false;

	/** A parameter to switch between Interchange import from DNA or content mesh duplication */
	bool bCreateMeshFromDNA = false;

	/** An outer package that should be used for created skeletal meshes */
	TNotNull<UObject*> OuterForGeneratedAssets = GetTransientPackage();

	/** The preview material type to be used */
	EMetaHumanCharacterSkinPreviewMaterial PreviewMaterial  = EMetaHumanCharacterSkinPreviewMaterial::Default;
};


/**
 * Subsystem used to interface with the UMetaHumanCharacter asset.
 * Any edits to a MetaHumanCharacter that may need to be exposed as an API
 * should be done as part of this class, as UFUNCTIONs declared here are automatically
 * exposed
 */
UCLASS(BlueprintType)
class METAHUMANCHARACTEREDITOR_API UMetaHumanCharacterEditorSubsystem
	: public UEditorSubsystem
	, public FTickableEditorObject
{
	GENERATED_BODY()

public:

	//~FTickableEditorObject interface
	virtual bool IsTickable() const override;
	virtual void Tick(float InDeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~End of FTickableEditorObject interface

public:
	//
	// Subsystem Initialization
	//
	//~Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~End USubsystem interface

	/**
	 * Utility for obtaining a pointer to the global instance of this subsystem in the editor.
	 */
	static UMetaHumanCharacterEditorSubsystem* Get();

	/**
	 * Registers an object to be edited. The first object registered will
	 * also load the Texture Synthesis model to make it to be used
	 * 
	 * Most functions taking a Character on this class require the Character to be registered for
	 * editing first.
	 * 
	 * Call RemoveObjectToEdit when done editing. If TryAddObjectToEdit returns false, the 
	 * Character is not registered, so there's no need to call RemoveObjectToEdit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Initialization")
	[[nodiscard]] bool TryAddObjectToEdit(UMetaHumanCharacter* InCharacter);

	/** Returns true if the object is registered for editing */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Initialization")
	bool IsObjectAddedForEditing(const UMetaHumanCharacter* InCharacter) const;

	/**
	 * Tells the subsystem that a character is no longer being edited.
	 * Unloads the texture synthesis model when the last object being
	 * edited is removed from the subsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Initialization")
	void RemoveObjectToEdit(const UMetaHumanCharacter* InCharacter);

	/**
	* Clears all internal model data for Texture Synthesis and re-loads the model using the path in the settings
	*/
	void ResetTextureSynthesis();

	/** Runs the editor pipeline (Preview quality) for the given character. Use whenever changes are made that should be reflected in the preview */
	UFUNCTION(BlueprintCallable, Category = "Pipeline", meta = (ScriptName="AssembleForPreview"))
	void RunCharacterEditorPipelineForPreview(UMetaHumanCharacter* InCharacter) const;

	/** Gets a readonly view on the character editor data */
	const TSharedRef<FMetaHumanCharacterEditorData>* GetMetaHumanCharacterEditorData(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
private:
	/**
	 * Initializes the editing state for a Character without registering it.
	 *
	 * Textures are guaranteed to be created by this function, but not necessarily filled with
	 * correct image data yet unless bBlockUntilComplete is true.
	 *
	 * @param InCharacter The character to create the editor data for
	 * @param InParams The collection of input parameters that affect editor data creation
	 * @param OutSynthesizedFaceTextures will receive the set of textures for the Character.
	 * @param OutBodyTextures will receive the set of bodytextures for the Character.
	 * @param InFaceTextureSynthesizerLoadTask optional, task to track whether the FaceTextureSynthesizer has finished initialization
	 */
	TSharedPtr<FMetaHumanCharacterEditorData> CreateEditorDataForCharacter(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		const FEditorDataForCharacterCreationParams& InParams,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<class UTexture2D>>& OutBodyTextures,
		UE::Tasks::FTask InFaceTextureSynthesizerLoadTask = {});

	/**
	* Creates facial SkeletalMesh from DNA through DNA Interchange system and attaches it to CharacterData->FaceMesh.
	*/
	static void UpdateCharacterFaceMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData);

	/**
	* Creates body SkeletalMesh from DNA through DNA Interchange system and attaches it to CharacterData->BodyMesh.
	*/
	static void UpdateCharacterBodyMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData);
	
	//* Setting up and returning the Face and Body states for the character */
	bool InitializeIdentityStateForFaceAndBody(TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedPtr<FMetaHumanCharacterIdentity::FState>& OutFaceState, TSharedPtr<FMetaHumanCharacterBodyIdentity::FState>& OutBodyState);

	/** Creates Face and Body mesh either by duplicating content browser assets or Interchange system from stored or loaded DNA data */
	static void GetFaceAndBodySkeletalMeshes(TNotNull<const UMetaHumanCharacter*> InCharacter, const FEditorDataForCharacterCreationParams& InParams, USkeletalMesh*& OutFaceMesh, USkeletalMesh*& OutBodyMesh);
	
	/** 
	 * Fills in textures with image data for any pending textures that are ready.
	 * 
	 * This should be called repeatedly while textures are pending.
	 */
	static void UpdatePendingSynthesizedTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures);

	static void UpdatePendingHighResBodyTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures);

	/** Block until all textures are filled with image data */
	static void WaitForSynthesizedTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter, 
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures);

	/** Updates thumbnail assets for the given character. */
	void SaveCharacterThumbnails(TNotNull<UMetaHumanCharacter*> InCharacter);

public:
	//
	// Character and Actor Initialization
	//

	/**
	 * Initializes all properties from the given MetaHumanCharacter that require loading data from various sources
	 */
	void InitializeMetaHumanCharacter(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Spawns and initializes an actor implementing IMetaHumanCharacterEditorActorInterface in the given world.
	 * 
	 * The actor will have the all of its components initialized from the state stored in the MetaHumanCharacter Asset.
	 * 
	 * This function will try to spawn the actor specified by the selected MetaHuman Character Pipeline, but falls back
	 * to a default actor type if that fails, so it's guaranteed to return a valid actor.
	 */
	TScriptInterface<IMetaHumanCharacterEditorActorInterface> CreateMetaHumanCharacterEditorActor(TNotNull<UMetaHumanCharacter*> InCharacter, TNotNull<class UWorld*> InWorld);

	/**
	 * @brief Spawns a MetaHuman Editor Actor in the main editor level
	 * 
	 * The spawned actor will reflect any changes made to the character while its added to the subsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Actor")
	AActor* SpawnMetaHumanActor(UMetaHumanCharacter* InCharacter);

	/** 
	 * Gets the class of actor that will be spawned by CreateMetaHumanCharacterEditorActor if there
	 * are no errors.
	 * 
	 * If CreateMetaHumanCharacterEditorActor would fall back to spawning a default actor type, this 
	 * function will return false and OutActorClass will be set to null.
	 */
	[[nodiscard]] bool TryGetMetaHumanCharacterEditorActorClass(TNotNull<const UMetaHumanCharacter*> InCharacter, TSubclassOf<AActor>& OutActorClass, FText& OutFailureReason) const;

	/**
	 * Create invisible driving actor.
	 * The invisible driving actor is used to play preview animations on the archetype skeletal meshes for which our animations have been recorded for. This is needed for retargeting.
	 * We use the invisible driving actor to drive the pose in the right proportions and then retarget it onto the preview MetaHuman. This avoids artefacts from inline retargeting while
	 * we can leave the MH Blueprint like it is. Curves will be propagated over as well.
	 */
	void CreateMetaHumanInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TScriptInterface<IMetaHumanCharacterEditorActorInterface> InEditorActorInterface, TNotNull<class UWorld*> InWorld);

	/**
	 * Get the invisible driving actor given the character.
	 */
	TObjectPtr<class AMetaHumanInvisibleDrivingActor> GetInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Removes all data that is not needed to be in the character to make it a preset.
	 * Removes any stored textures and rigs.
	 * The caller is responsible for making sure the character is not opened for edit, returns false if the conversion failed
	 */
	bool RemoveTexturesAndRigs(TNotNull<UMetaHumanCharacter*> InCharacter);

public:
	//
	// Build and Export
	//

	/**
	 * Generates assets, such as meshes and textures, so that other code systems can render the 
	 * Character.
	 * 
	 * All generated objects must have the provided InOuterForGeneratedAssets as their Outer, and
	 * be added to the Metadata array on OutGeneratedAssets. If InOuterForGeneratedAssets is
	 * nullptr, the Transient Package will be used as an Outer.
	 * 
	 * If asset generation fails, the function will return false and OutGeneratedAssets will be 
	 * empty. Some assets may have been generated but they will not be referenced from 
	 * OutGeneratedAssets.
	 */
	[[nodiscard]] bool TryGenerateCharacterAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		UObject* InOuterForGeneratedAssets,
		FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets);

	[[nodiscard]] bool TryGenerateCharacterAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		UObject* InOuterForGeneratedAssets,
		const FMetaHumanCharacterGeneratedAssetOptions& InOptions,
		FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets);

	/** 
	 * Fetches editor-owned assets needed for the preview build, such as the meshes being actively
	 * edited by the Character asset editor.
	 * 
	 * These assets are still owned by the editor and must NOT be modified by callers of this 
	 * function.
	 */
	[[nodiscard]] bool TryGetCharacterPreviewAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		FMetaHumanCharacterPreviewAssets& OutPreviewAssets);

	[[nodiscard]] bool TryGetCharacterPreviewAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterPreviewAssetOptions& InOptions,
		FMetaHumanCharacterPreviewAssets& OutPreviewAssets);

	/**
	 * Checks if MetaHuman character is ready for building. If the character cannot be built,
	 * outputs the error message describing the reason why.
	 */
	bool CanBuildMetaHuman(TNotNull<const UMetaHumanCharacter*> InCharacter, FText& OutErrorMessage);

	/**
	 * @brief Checks if MetaHuman character is ready for assembly.
	 *
	 * If the character is not ready a message with the reason why 
	 * can be printed in the logs using bInLogError.
	 * 
	 * @param InCharacter The character to verify
	 * @param bInLogError Set to log an error message if the character can't be built
	 * @return true if the character can be built and false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	bool CanBuildMetaHuman(const UMetaHumanCharacter* InCharacter, bool bInLogError = false);

	/**
	 * @brief Assemble a MetaHuman Character.
	 * 
	 * Which type of assembly and configuration options can be set using InParams
	 * 
	 * @param InCharacter The character to be assembled. The character must be opened for edit or the build will fail
	 * @param InParams Parameter that determine which type of build to make
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	void BuildMetaHuman(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEditorBuildParameters& InParams);

	/**
	 * Obtain a copy of the face and body materials used by the character
	 */
	void GetMaterialSetForCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter, FMetaHumanCharacterFaceMaterialSet& OutFaceMaterials, UMaterialInstanceDynamic*& OutBodyMaterial);

	/** Returns the material to apply to clothing when it should be translucent */
	class UMaterialInterface* GetTranslucentClothingMaterial() const;

	/* Sets the clothing visibility state on any character actor and optionally updates the body material with character data hidden face map */
	void SetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanClothingVisibilityState InState, bool bUpdateMaterialHiddenFaces);

	/* Gets the clothing visibility state on any character actor */
	EMetaHumanClothingVisibilityState GetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Returns true when the input Character has an outfit selected in the collection
	 */
	static bool IsCharacterOutfitSelected(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter);

	/**
	 * Returns the Face Archetype Mesh for the given template type
	 */
	static USkeletalMesh* GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType);

	/**
	 * Returns the Body Archetype Mesh for the given template type
	 */
	static USkeletalMesh* GetBodyArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType);

	/**
	 * Returns combined face and body mesh for the given character.
	 *
	 * The requirement is that the character has both face and body DNAs.
	 */
	USkeletalMesh* CreateCombinedFaceAndBodyMesh(TNotNull<const UMetaHumanCharacter*> InCharacter, const FString& InAssetPathAndName);

public:
	//
	// Skin Material Editing
	//

	/**
	 * Returns if the subsystem is able synthesize textures
	 */
	bool IsTextureSynthesisEnabled() const;

	/**
	 * Gets the skin tone texture color as rgb value
	 */
	FLinearColor GetSkinTone(const FVector2f& UV) const;

	/**
	 * Get or create the skin tone texture suitable to be used in the skin tone picker UI
	 * The caller is responsible for keeping a reference to the returned texture or it may be GC'ed
	 */
	TWeakObjectPtr<class UTexture2D> GetOrCreateSkinToneTexture();

	/**
	 * Estimates the skin tone UI values from an sRGB colour.
	 * Note that the estimation will be done using the currently loaded texture synthesis model.
	 * @param InSkinTone is assumed to be in sRGB space
	 */
	FVector2f EstimateSkinTone(const FLinearColor& InSkinTone, const int HFIndex) const;

	/**
	 * Get the maximum value for the HF index the model supports
	 */
	int32 GetMaxHighFrequencyIndex() const;

	/**
	 * Updates the face evaluation settings (vertex deltas and vertex geometry delta) of all the actors associated with the given character.
	 */
	void ApplyFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings) const;

	/**
	 * Set all the face evaluation settings to the character and apply the changes to all the registered actors.
	 */
	void CommitFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings);

	/**
	 * Gets the texture attribute map associated with the face texture synthesizer.
	 */
	const FMetaHumanFaceTextureAttributeMap& GetFaceTextureAttributeMap() const;
	
	/**
	 * Updates the Head Model (Eyelashes) of all the actors associated with the given character.
	 */
	void ApplyHeadModelSettings(
		TNotNull<UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings) const;

	/**
	 * Set all the Head Model settings to the character and apply the changes to all the registered actors.
	 */
	UFUNCTION(BlueprintCallable, Category = "Model")
	void CommitHeadModelSettings(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings);

	/**
	 * Applies or removes eyelashes grooms according to properties.
	 */
	void ToggleEyelashesGrooms(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties) const;
	
	/**
	 * Updates the Skin material of all the actors associated with the given character.
	 */
	void ApplySkinSettings(
		TNotNull<UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterSkinSettings& InSkinSettings) const;

	/**
	 * Set all the skin settings to the character and apply the changes to all registered actors.
	 * This will synthesize textures if needed based on skin settings. This will also discard any
	 * high resolution textures currently stored in the Character
	 */
	UFUNCTION(BlueprintCallable, Category = "Texture Synthesis")
	void CommitSkinSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	
	UE_DEPRECATED(5.7, "Please use RequestTextureSources") 
	void RequestHighResolutionTextures(TNotNull<UMetaHumanCharacter*> InCharacter, ERequestTextureResolution InResolution);

	/**
	 * @brief Request high resolution textures for the given character.
	 *
	 * This function does nothing if there is already a pending request
	 *
	 * @param InCharacter The character to request the textures for
	 * @param InParams Parameters to control the request
	 */
	UFUNCTION(BlueprintCallable, Category = "Texture Synthesis")
	void RequestTextureSources(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterTextureRequestParams& InParams = FMetaHumanCharacterTextureRequestParams());

	/**
	 * Returns true if there is pending request for high resolution textures
	 */
	bool IsRequestingHighResolutionTextures(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Update the currently active preview material for the character
	 */
	void UpdateCharacterPreviewMaterial(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterial) const;

	/**
	* Compare the face textures for the two supplied characters
	* Does NOT require the characters to have been added for edit
	* Returns true if face textures are identical to within the specified tolerance for each channel of each pixel, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "Testing")
	static bool CompareFaceTextures(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, int32 InPixelTolerance);


	/** Callback when downloading textures state changes in editor */
	DECLARE_MULTICAST_DELEGATE_OneParam(FMetaHumanOnDownloadingTexturesStateChanged, TNotNull<const UMetaHumanCharacter*>);
	FMetaHumanOnDownloadingTexturesStateChanged OnDownloadingTexturesStateChanged;

private:

	/**
	 * Stores the synthesized textures in the character asset to be serialized
	 */
	void StoreSynthesizedTextures(TNotNull<UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Update the preview material for the actors corresponding to the character data
	 */
	static void UpdateActorsSkinPreviewMaterial(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInUseFaceMaterialsWithVTSupport);

	/** 
	 * Updates the editing state of the Character with the given skin settings.
	 * 
	 * Compares the new skin settings to those in the Character Data to determine whether to
	 * re-synthesize textures, etc.
	 * 
	 * If bInForceUseExistingTextures is true, this function will assume the current textures are
	 * up to date and will not re-synthesize them even if the new skin settings don't match the 
	 * stored settings. 
	 * 
	 * bOutTexturesHaveBeenRegenerated will be set to true if textures were re-synthesized.
	 */
	void ApplySkinSettings(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		const FMetaHumanCharacterSkinSettings& InSkinSettings,
		bool bInForceUseExistingTextures,
		const FMetaHumanCharacterSkinTextureSet& InFinalSkinTextureSet,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& InOutBodyTextures,
		bool& bOutTexturesHaveBeenRegenerated) const;

	/**
	 * Synthesizes textures and updates face state with high frequency data.
	 *
	 * This function doesn't compare the new state to the existing state, so only call it if the
	 * textures and HF data need updating.
	 *
	 * @param InCharacterData the character data struct to apply the settings to
	 * @param InSkinProperties parameters for the texture synthesis model
	 * @param InOutSynthesizedFaceTextures synthesized textures to be updated
	 * @param InOutBodyTextures the set of body textures to be updated and update the materials with
	 */
	void ApplySkinProperties(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		const FMetaHumanCharacterSkinProperties& InSkinProperties,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& InOutBodyTextures) const;

	/** Updates material parameters to set textures and skin tone. Needs to be called if new texture objects are being used. */
	void UpdateSkinTextures(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
							const FMetaHumanCharacterSkinProperties& InSkinProperties,
							const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const;

	/**
	 * Handles a high resolution texture response.
	* Stores the new textures in the character and update any live character actors
	*/
	void OnHighResolutionTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FFaceHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution texture request failure
	 */
	void OnHighResolutionTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of the texture download notification
	 */
	void OnHighResolutionTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution body texture response.
	* Stores the new textures in the character and update any live character actors
	*/
	void OnHighResolutionBodyTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FBodyHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution body texture request failure
	 */
	void OnHighResolutionBodyTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of the body texture download notification
	 */
	void OnHighResolutionBodyTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

public:
	//
	// Eyes editing
	//

	/**
	 * Updates the editing state of the Character with the given eyes settings
	 */
	void ApplyEyesSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const;

	/**
	 * @brief Commits the eyes settings to the character and updates the associated actors
	 * 
	 * @param InCharacter The character in which the eyes settings are going to be committed
	 * @param InEyesSettings the eyes settings to commit to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "Eyes")
	void CommitEyesSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const;

private:

	/**
	 * Utility function to apply the eyes settings in the character data and update the eyes material with the eyes settings
	 */
	static void ApplyEyesSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyesSettings& InEyesSettings);

public:
	//
	// Makeup editing
	//

	/**
	 * Updates the editing state of the Character with the given makeup settings
	 */
	void ApplyMakeupSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const;

	/**
	 * @brief Commits the makeup settings to the character and updates the associated actors
	 * 
	 * @param InCharacter The character in which the makeup settings are going to be commited
	 * @param InMakeupSettings the makeup settings to commit to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "Makeup")
	void CommitMakeupSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const;

private:

	/**
	 * @brief Utility function to apply the makeup settings in the character data and update the face material with the makeup settings
	 * 
	 * @param InCharacterData Character Editor Data containing the materials to be updated
	 * @param InMakeupSettings The makeup settings to apply to the materials
	 * @param bInWithVTSupport Whether or not load virtual textures for slots that support it
	 */
	static void ApplyMakeupSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterMakeupSettings& InMakeupSettings, bool bInWithVTSupport);

public:
	//
	// Face sculpting and editing
	//

	/**
	 * Applies the given state in the MetaHuman Character Actors registered against the character.
	 */
	void ApplyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState);

	/**
	 * Provides read-only access to the current face editing state.
	 * 
	 * If edits have been made since the last call to CommitFaceState, this will be different from
	 * Character's stored face state.
	 */
	TSharedRef<const FMetaHumanCharacterIdentity::FState> GetFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	
	/**
	 * Provides read-only access to the FDNAToSkelMeshMap for the current face editing state.
	 */
	TSharedRef<const FDNAToSkelMeshMap> GetFaceDnaToSkelMeshMap(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Creates a copy of the current face editing state.
	 * 
	 * Same as GetFaceState, but creates a copy owned by the caller for convenience.
	 */
	[[nodiscard]] TSharedRef<FMetaHumanCharacterIdentity::FState> CopyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Commits the Face State into the Character asset in order to be serialized when the asset is saved.
	 * 
	 * Also updates the face editing state.
	 */
	void CommitFaceState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState);

	/**
	 * Commits the Face State of the given Character to be serialized. This needs to be done after after using the
	 * sculpting or blending APIs to persist the changes
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 */
	UFUNCTION(BlueprintCallable, Category = "Sculpting")
	void CommitFaceState(UMetaHumanCharacter* InCharacter);

	/**
	* Evaluate the face state for each of the supplied characters, and compare the vertices and vertex normals
	* Requires the characters to have been added for edit using TryAddObjectToEdit
	* Returns true if all corresponding vertices and vertex normals are within InTolerance of each other in terms of vector norm
	*/
	UFUNCTION(BlueprintCallable, Category = "Testing")
	bool CompareFaceState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const;


	/** 
	 * Returns a reference to a delegate that fires whenever the face editing state of the given character is modified.
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FSimpleMulticastDelegate& OnFaceStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Updates the face editing state and SkelMesh with the given DNA. Returns a ptr to the updated DNA if succeeds, nullptr otherwise
	 * InLodUpdateOption allows you choose which lods in the SkelMesh are updated.
	 * bInResettingToArchetypeDNA allows you to pass a special flag which indicates if we are resetting to the archetype placeholder DNA, in which case, we do not need
	 * the face state to match the DNA
	 */
	TSharedPtr<IDNAReader> ApplyFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader, ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All, bool bInResettingToArchetypeDNA = false);

	/**
	 * Create a face skeletal mesh from the imported DNA.
	 */
	void ImportFaceDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader);

	/**
	 * Enable skeletal post-processing.
	 * This will enable running the face and body rig and correctives.
	 */
	UE_DEPRECATED(5.7, "Enabling animation is now handled internally by the actors and this function is not needed anymore")
	void EnableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Disable skeletal post-processing.
	 * This will enable running the face and body rig and correctives.
	 */
	UE_DEPRECATED(5.7, "Disabling animation is now handled internally by the actors and this function is not needed anymore")
	void DisableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Enable animation.
	 * This will connect the preview character to the invisible driving actor.
	 */
	UE_DEPRECATED(5.7, "Enabling animation is now handled internally by the actors and this function is not needed anymore")
	void EnableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Disable animation.
	 * This will disconnect the preview character from the invisible driving actor.
	 */
	UE_DEPRECATED(5.7, "Disabling animation is now handled internally by the actors and this function is not needed anymore")
	void DisableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter);
	
	/**
	 * Commits the Face DNA into the Character asset in order to be serialized when the asset is saved.
	 */
	void CommitFaceDNA(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDNAReader);

	/**
	 * Reset character face.
	 */
	void ResetCharacterFace(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Returns the list of Face Gizmo positions from the Character's state
	 */
	[[nodiscard]] TArray<FVector3f> GetFaceGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Sets the face gizmo to an exact position.
	 * This function updates the character's Face mesh and returns the list of updated gizmo positions
	 */
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoPosition(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, const FVector3f& InPosition, bool bInSymmetric, bool bInEnforceBounds);

	/**
	 * Sets the face gizmo to an exact rotation.
	 * This function updates the character's Face mesh and returns the list of updated gizmo positions
	 */
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoRotation(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, const FVector3f& InRotation, bool bInSymmetric, bool bInEnforceBounds);

	/**
	* Scales the given gizmo.
	* This function updates the character's Face mesh and returns the list of updated gizmo positions
	*/
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoScale(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, float InScale, bool bInSymmetric, bool bInEnforceBounds);

	/**
	 * Returns the list of Face Landmark positions from the Character's state
	 */
	[[nodiscard]] TArray<FVector3f> GetFaceLandmarks(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * @brief Gets the positions of the face landmarks for a given character.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to obtain the landmarks for
	 * @param OutFaceLandmarks the list with the face landmarks positions
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sculpting")
	void GetFaceLandmarks(const UMetaHumanCharacter* InCharacter, TArray<FVector>& OutFaceLandmarks) const;

	/**
	 * Translates the given landmark by a delta.
	 * This function updates the character's Face mesh and returns the list of updated landmark positions
	 */
	[[nodiscard]] TArray<FVector3f> TranslateFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InLandmarkIndex, const FVector3f& InDelta, bool bInTranslateSymmetrically);

	/**
	 * @brief Translates the face landmarks by the given deltas
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * The number of landmark indices and deltas must match
	 * 
	 * 
	 * @param InCharacter the character to translate the landmarks for
	 * @param InLandmarkIndices The index of each landmark to be translated
	 * @param InDeltas the list of translation deltas to apply to each landmark
	 */
	UFUNCTION(BlueprintCallable, Category = "Sculpting")
	void TranslateFaceLandmarks(const UMetaHumanCharacter* InCharacter, const TArray<int32>& InLandmarkIndices, const TArray<FVector>& InDeltas);

	/**
	 * Selects a vertex on the face by intersecting the ray with the current face mesh.
	 */
	int32 SelectFaceVertex(TNotNull<const UMetaHumanCharacter*> InCharacter, const FRay& InRay, FVector& OutHitVertex, FVector& OutHitNormal);

	/**
	 * Adds additional custom landmark manipulator on a given mesh surface point.
	 */
	void AddFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InMeshVertexIndex);

	/**
	 * Removes selected landmark manipulator.
	 */
	void RemoveFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InLandmarkIndex);

	/**
	 * Blends Face region though preset states.
	 */
	TArray<FVector3f> BlendFaceRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, const TSharedPtr<const FMetaHumanCharacterIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights, EBlendOptions InBlendOptions, bool bInBlendSymmetrically);

	/**
	 * Method which handles calls to AutoRigService.
	 */
	UE_DEPRECATED(5.7, "Please use RequestAutoRigging")
	void AutoRigFace(TNotNull<UMetaHumanCharacter*> InCharacter, const UE::MetaHuman::ERigType InRigType);

	/**
	 * @brief Make a request to auto-rig a character
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to be auto-rigged
	 * @param InParams Parameters to control the auto-rigging process
	 */
	UFUNCTION(BlueprintCallable, Category = "Auto-Rigging")
	void RequestAutoRigging(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterAutoRiggingRequestParams& InParams = FMetaHumanCharacterAutoRiggingRequestParams());

	/**
	 * Remove the face rig from InCharacter
	 */
	void RemoveFaceRig(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Remove the body rig from InCharacter
	 */
	void RemoveBodyRig(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Utility function that sets the eyelashes variant to the input state based on the eyelashes type property
	 */
	void UpdateEyelashesVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties) const;

	/**
	 * Utility function that sets the teeth variant to the input state based on the teeth type property
	 * Also allows the user to turn off the (teeth) expressions at the end of using the tool
	 */
	void UpdateTeethVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterTeethProperties& InTeethProperties, bool bInUseExpressions = true) const;

	/**
	 * Utility function that sets the high frequency variant to the input state based on the skin texture property
	 */
	void UpdateHFVariantFromSkinProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterSkinProperties& InSkinProperties) const;

	/**
	 * Returns true if there is an active request to auto rig the face of the given character
	 */
	bool IsAutoRiggingFace(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Get the rigging state for the supplied character
	 */
	EMetaHumanCharacterRigState GetRiggingState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Fit the state to the supplied target vertices, which will use whatever part(s) the user has supplied to fit the model
	 * EHeadFitToTargetMeshes::Head vertices must always be supplied in the InTargetVertices, using the supplied fitting options.
	 * Returns true if successful, false otherwise
	 */
	bool FitStateToTargetVertices(TNotNull<UMetaHumanCharacter*> InCharacter, const TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& InTargetVertices, const FFitToTargetOptions & InFitToTargetOptions);

	/**
	 * @brief Fit the character to the supplied vertices.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to be fitted
	 * @param InParams Parameters to control the fitting process as well as the meshes
	 * @return true if successful, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	bool FitStateToTargetVertices(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterFitToVerticesParams& InParams);

	/**
	 * Fit the state to the supplied face DNA, using the supplied fitting options. Returns true if successful, false otherwise
	 */
	bool FitToFaceDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<class IDNAReader> InFaceDna, const FFitToTargetOptions& InFitToTargetOptions);

	/**
	 * Fits the Character face state to the conformed mesh of the input Identity asset
	 */
	EImportErrorCode ImportFromIdentity(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<const class UMetaHumanIdentity*> InMetaHumanIdentity, const FImportFromIdentityParams& InImportParams);

	/**
	 * Either fits the Character face state to the input face DNA, or imports the DNA as-is, depending on options
	 */
	EImportErrorCode ImportFromFaceDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDna, const FImportFromDNAParams& InImportParams);

	/**
	 * @brief Either fits the Character face state to the input face DNA, or imports the DNA as-is, depending on options
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter The character to import the DNA to
	 * @param InDNAFilePath Path to the DNA file to be imported
	 * @param InImportParams Options for how to perform the import operation
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	EImportErrorCode ImportFromFaceDna(UMetaHumanCharacter* InCharacter, const FString& InDNAFilePath, const FImportFromDNAParams& InImportParams);


	/**
	 * @brief Fits the Character face state to the conformed mesh of the input asset, which must be a SkelMesh or Static Mesh which has the correct number of vertices.
	 *
	 * In addition, the user can (optionally) in the case of a StaticMesh pass in up to three additional meshes for left eye, right eye and teeth, which if not
	 * null will be used in the fitting. Note that for the StaticMesh, if the extra meshes are present, they will be used and the flags in the import options will be ignored. 
	 * Eye and Teeth meshes must contain the correct number of vertices for a MetaHuman.
	 *
	 * @param InCharacter The character to import the template mesh(es) to
	 * @param InTemplateMesh The head mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman head mesh
	 * @param InTemplateLeftEyeMesh If not nullptr, must be a StaticMesh for the left eye which much match the topology of a MetaHuman left eye mesh
	 * @param InTemplateRightEyeMesh If not nullptr, must be a StaticMesh for the right eye which much match the topology of a MetaHuman right eye mesh
	 * @param InTemplateTeethMesh If not nullptr, must be a StaticMesh for the teeth which much match the topology of a MetaHuman teeth mesh
	 * @param InImportParams the parameters used during the fitting process
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	EImportErrorCode ImportFromTemplate(UMetaHumanCharacter* InCharacter, UObject* InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh, 
		const FImportFromTemplateParams& InImportParams);


	/**
	 * Initializes metahuman character using selected preset character.
	 */
	void InitializeFromPreset(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<UMetaHumanCharacter*> InPresetCharacter);

private:
	/**
	 * Called when an AutoRigging request completes
	 */
	void OnAutoRigFaceRequestCompleted(const UE::MetaHuman::FAutorigResponse& InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey, const UE::MetaHuman::ERigType InRigType);

	/**
	 * Handles a high resolution texture request failure
	 */
	void OnAutoRigFaceRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of an AutoRigging request
	 */
	void OnAutoRigFaceProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Sets the given face state on the Character Data.
	 * 
	 * Note that this function takes ownership of InState, unlike the public overload that takes a copy of it.
	 */
	static void ApplyFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TSharedRef<FMetaHumanCharacterIdentity::FState> InState);
	
	/** Updates the face editing state from the given skin properties */
	void ApplySkinPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterSkinProperties& InSkinProperties) const;

	/** Updates the face editing state from the given eyelashes and teeth properties */
	void ApplyEyelashesAndTeethPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties, const FMetaHumanCharacterTeethProperties& InTeethProperties, 
		bool bInUpdateEyelashes, bool bInUpdateTeeth, ELodUpdateOption InUpdateOption) const;

	/**
	 * Updates the Face Mesh of the Character using state stored in the actor and the given vertices and vertex normals
	 * This function does not evaluate the model and purely updates the skeletal mesh. It is the caller
	 * responsibility to call Evaluate and obtain the vertices and normals to pass to this function.
	 */
	static void UpdateFaceMeshInternal(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanRigEvaluatedState& InVerticesAndNormals, ELodUpdateOption InUpdateOption);


	/**
	 * Get the data for performing import from template mesh
	 */
	EImportErrorCode GetDataForConforming(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<UObject*> InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh, const FImportFromTemplateParams& InImportParams, TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& OutVertices);


public:
	//
	// Body Editing
	//

	enum class EBodyMeshUpdateMode : uint8
	{
		/** A fast update to be used while dragging sliders, etc. Only the data needed for immediate rendering is updated. */
		Minimal,
		/** The full update that takes longer. This must be done once the slider drag or other input is complete. */
		Full
	};

	/**
	 * Applies the given custom body state to MetaHuman Character Actors registered against the character.
	 * Evaluates the state and updates the body mesh, updates the character's body mesh state using the state stored in the character
	 * 
	 * The subsystem takes a copy of the passed-in state and uses the copy, so InState will not be modified.
	 */
	void ApplyBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode);

	/**
	* Commits the Body State into the Character asset in order to be serialized when the asset is saved.
	* If there are live Character actors registered against the subsystem, also update their face state.
	*/
	void CommitBodyState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode = EBodyMeshUpdateMode::Full);

	/**
	* Evaluate the body state for each of the supplied characters, and compare the vertices and vertex normals
	* Requires the characters to have been added for edit using TryAddObjectToEdit
	* Returns true if all corresponding vertices and vertex normals are within InTolerance of each other in terms of vector norm
	*/
	UFUNCTION(BlueprintCallable, Category = "Testing")
	bool CompareBodyState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const;

	/**
	 * Commits the Body State of the given Character to be serialized. This needs to be done after after using the
	 * sculpting or blending APIs to persist the changes
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 */
	UFUNCTION(BlueprintCallable, Category = "Sculpting")
	void CommitBodyState(UMetaHumanCharacter* InCharacter);

	/** 
	 * Returns a reference to a delegate that fires whenever the body editing state of the given character is modified.
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FSimpleMulticastDelegate& OnBodyStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	
	/**
	 * Provides read-only access to the current body editing state.
	 * 
	 * If edits have been made since the last call to CommitBodyState, this will be different from
	 * Character's stored body state.
	 */
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> GetBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Creates a copy of the current body editing state.
	 * 
	 * Same as GetBodyState, but creates a copy owned by the caller for convenience.
	 */
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> CopyBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Sets the body vertex and joint global delta scale
	 */
	void SetBodyGlobalDeltaScale(TNotNull<UMetaHumanCharacter*> InCharacter, float InBodyGlobalDelta) const;

	/**
	 * Gets the body vertex and joint global delta scale
	 */
	float GetBodyGlobalDeltaScale(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	* Updates the body editing state with the given DNA. Optionally importing the mesh from DNA via interchange
	*/
	TSharedPtr<IDNAReader> ApplyBodyDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InBodyDNAReader, bool bImportMesh = true);

	/**
	 * Commits the Body DNA into the Character asset in order to be serialized when the asset is saved,
	 * Sets the Character as having a fixed body type with bFixedBodyType. Fixed types import the mesh from DNA via interchange
	 */
	void CommitBodyDNA(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDNAReader, bool bFixedBodyType = true);

	/**
	*  Fits the Character body state to the fixed body DNA
	*/
	bool ParametricFitToDnaBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter);

	/**
	*  Fits the Character body state to the current fixed compatibility body
	*/
	bool ParametricFitToCompatibilityBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter);

#if WITH_EDITORONLY_DATA
	/**
	* Either fits the Character body state to the input body DNA, or imports the DNA as-is, depending on options
	*/
	UE_DEPRECATED(5.7, "ImportFromBodyDna option has been deprecated. Please use ConformBody, SetBodyJointTranslations, SetBodyMesh or ImportWholeRig.")
	EImportErrorCode ImportFromBodyDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, const FImportBodyFromDNAParams& InImportOptions);
	
	/**
	 * @brief Fits the Character body state to the conformed mesh of the input asset, which must be a SkelMesh or Static Mesh which has the correct number of vertices.
	 * 	 
	 * @param InMetaHumanCharacter The character to import the body template mesh to
	 * @param InTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman body or combined head and body mesh
	 * @param InBodyFitOptions the fitting options used during the fitting process
	 * @return code indicating success or failure
	 */
	UE_DEPRECATED(5.7, "ImportFromBodyTemplate option has been deprecatd. Please use ConformBody, SetBodyJointTranslations, SetBodyMesh or ImportWholeRig.")
	EImportErrorCode ImportFromBodyTemplate(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InTemplateMesh, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);
#endif // WITH_EDITORONLY_DATA

	/**
	 * Get the mesh for performing import from body template 
	 */
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector3f>& OutVertices);

	/**
	 * @brief Blueprint exposed version of the GetMeshForBodyConforming function
	 *
	 * @param InMetaHumanCharacter The character to import the body template mesh to
	 * @param InBodyTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman body or combined head and body mesh	 
	 * @param InHeadTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman head mesh
	 * @param bInMatchVerticesByUVs flag which if set to true uses UV matching to match the vertices, otherwise requires exactly matching SkelMesh
	 * @param OutVertices On completion contains the body mesh vertices
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector>& OutVertices);

	
	/**
	 * Get the mesh for performing import from body template
	 */
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, TSharedRef<IDNAReader> BodyDNA, TSharedPtr<IDNAReader> HeadDNA, TArray<FVector3f>& OutVertices);

	/**
	 * Get the joints for performing import from body template
	 */
	EImportErrorCode GetJointsForBodyConforming(USkeletalMesh* InTemplateMesh, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);

	/**
	 * @brief Blueprint exposed version of the GetMeshForBodyConforming function
	 *
	 * @param InTemplateMesh The mesh to be conformed which much match the topology of a MetaHuman body or combined head and body mesh
	 * @param OutJointWorldTranslations On completion contains the body joint world translations
	 * @param OutJointRotations On completion contains the body joint rotations
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	EImportErrorCode GetJointsForBodyConforming(USkeletalMesh* InTemplateMesh, TArray<FVector>& OutJointWorldTranslations, TArray<FVector>& OutJointRotations);


	/**
	 * Get the joints for performing import from body template
	 */
	EImportErrorCode GetJointsForBodyConforming(TSharedRef<IDNAReader> BodyDNA, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);

	
	/* Conforms the body mesh to target parameters */
	bool ConformBody(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bRepose, bool bEstimateJointsFromMesh);

	/**
	 * @brief Blueprint exposed version of the ConformBody function
	 *
	 * @param OutMetaHumanCharacter The output MetaHumanCharacter after body conformation
	 * @param InVertices The body vertices, obtained by calling GetMeshForBodyConforming
	 * @param InJointRotations The body joint rotations, obtained by calling GetJointsForBodyConforming
	 * @param bRepose If set to true, repose the body to the A pose after conformation, if false, leave as-is
	 * @param bEstimateJointsFromMesh If set to true, estimate the hand and feet joints from the supplied mesh vertices; otherwise, leave alone.
	 * @return true if successful, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Conforming")
	bool ConformBody(UMetaHumanCharacter* OutMetaHumanCharacter, const TArray<FVector>& InVertices, const TArray<FVector>& InJointRotations, bool bRepose, bool bEstimateJointsFromMesh);


	/* Set custom body joints */
	bool SetBodyJoints(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InComponentJointTranslations, const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints);

	/* Set custom body neutral mesh */ 
	bool SetBodyMesh(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, bool bRepositionHelperJoints);

	/* Import body as a fully rigged Character  from DNA, sets character to a fixed body type */
	EImportErrorCode ImportBodyWholeRig(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, TSharedPtr<IDNAReader> InHeadDna);

#if WITH_EDITORONLY_DATA
	/**
	 * Fit the state to the supplied body DNA. Returns true if successful, false otherwise
	 */
	UE_DEPRECATED(5.7, "FitToBodyDna option has been deprecatd. Please use ConformBodyFromDna with FConformBodyParams instead.")
	bool FitToBodyDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<class IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);
#endif // WITH_EDITORONLY_DATA

	/**
	 * @brief Gets the list of body constrains from the underlying parametric body model
	 * 
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter the character to retrieve the body constraints for
	 * @param bScaleMeasurementRangesWithHeight scale the measurement ranges by height to help stay within realistic model proportions
	 * @return the list of body constraints provided by the underlying parametric body model
	 */
	UFUNCTION(BlueprintCallable, Category = "Sculpting")
	TArray<FMetaHumanCharacterBodyConstraint> GetBodyConstraints(const UMetaHumanCharacter* InCharacter, bool bScaleMeasurementRangesWithHeight = false) const;

	/**
	 * @brief Set body constraints and evaluate the parametric body
	 * 
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter The character to update the constraints
	 * @param InBodyContrains The list of body constraints to apply to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "Sculpting")
	void SetBodyConstraints(const UMetaHumanCharacter* InCharacter, const TArray<FMetaHumanCharacterBodyConstraint>& InBodyConstraints);

	/**
	* Reset the parametric body
	*/
	void ResetParametricBody(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Sets the MetaHuman body on the body editing state
	 */
	void SetMetaHumanBodyType(TNotNull<const UMetaHumanCharacter*> InCharacter, EMetaHumanBodyType InBodyType, EBodyMeshUpdateMode InUpdateMode);

	/**
	* Is the body a fixed body type, either imported from dna as a whole rig, or a fixed compatibility body
	*/
	bool IsFixedBodyType(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Returns the list of body region gizmo positions from the character's state
	*/
	[[nodiscard]] TArray<FVector3f> GetBodyGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	* Blends Face region though preset states.
	*/
	TArray<FVector3f> BlendBodyRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, EBodyBlendOptions InBodyBlendOptions, const TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights);

	/** 
	 * DEBUG ONLY
	 * 
	 * These functions return the face and body editing meshes for the character. 
	 * 
	 * Tools should not need direct access to this.
	 */
	TNotNull<const USkeletalMesh*> Debug_GetFaceEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	TNotNull<const USkeletalMesh*> Debug_GetBodyEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

private:

	static void UpdateBodyMeshInternal(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
		const FMetaHumanRigEvaluatedState& InVerticesAndNormals, 
		ELodUpdateOption InUpdateOption, 
		bool bInUpdateDnaState);

	static void UpdateFaceFromBodyInternal(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
		ELodUpdateOption InUpdateOption,
		bool bInUpdateNeutral);
	
	static void ApplyBodyState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode);

	/*
	 * Updates the character's fixed body type, fixed bodies are either imported from dna as a whole rig, or a fixed compatibility body
	 */
	void UpdateCharacterIsFixedBodyType(TNotNull<UMetaHumanCharacter*> InCharacter, bool bIsFixedBody);

	/**
	* Utility function that invokes a callback for each valid MetaHuman Character Editor Actor registered against the given MetaHuman Character
	 */
	static void ForEachCharacterActor(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TFunction<void(TScriptInterface<class IMetaHumanCharacterEditorActorInterface>)> InFunc);

	struct FMetaHumanCharacterIdentityModels
	{
		TSharedPtr<FMetaHumanCharacterIdentity> Face;
		TSharedPtr<FMetaHumanCharacterBodyIdentity> Body;
	};

	/**
	 * Returns the FMetaHumanCharacterIdentity of the given template type.
	 * If the Identity for the template doesn't exist it will be created and cached in CharacterIdentities
	 */
	const FMetaHumanCharacterIdentityModels& GetOrCreateCharacterIdentity(EMetaHumanCharacterTemplateType InTemplateType);

	/**
	 * Returns the path to where the face models for the given template type are stored
	 */
	static FString GetFaceIdentityTemplateModelPath(EMetaHumanCharacterTemplateType InTemplateType);

	/** 
	 * Returns the path to where the body model is stored
	 */
	static FString GetBodyIdentityModelPath();

	/** 
	 * Returns the path to where the legacy bodies are stored
	 */
	static FString GetLegacyBodiesPath();

	/**
	 * Creates the physics asset using body state
	 *
	 * @param InCharacter the character to generate the physics asset for
	 * @param InOuter the object to use as the outer for the new physics asset
	 * @param InBodyState the body state used to generate the physics asset
	 */
	static TObjectPtr<UPhysicsAsset> CreatePhysicsAssetForCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter,
																	TNotNull<UObject*> InOuter,
																	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);
	/**
	 * Updates a physics asset using body state
	 */
	static void UpdatePhysicsAssetFromBodyState(TNotNull<UPhysicsAsset*> InPhysicsAsset, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);

	/**
	 * Called on character instance updated. Updates head and body hidden face maps and materials
	 */
	void OnCharacterInstanceUpdated(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Updates hidden faces map on body material
	 */
	void UpdateCharacterPreviewMaterialHiddenFacesMask(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	* Gets measurements from face and body, either from dna or from character state
	*/
	TMap<FString, float> GetFaceAndBodyMeasurements(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, bool bGetMeasuresFromDNA) const;

private:

	static ELodUpdateOption GetUpdateOptionForEditing();

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// Map a MetaHuman Character to the data it needs while being edited
	TMap<TObjectKey<UMetaHumanCharacter>, TSharedRef<FMetaHumanCharacterEditorData>> CharacterDataMap;

	// Map all the live cloud requests for a given MetaHuman Character
	TMap<TObjectKey<UMetaHumanCharacter>, FMetaHumanCharacterEditorCloudRequests> CharacterCloudRequests;

	// Map with loaded Character Identity Models
	TSortedMap<EMetaHumanCharacterTemplateType, FMetaHumanCharacterIdentityModels> CharacterIdentities;

	// Face Synthesizer to be shared between all editable objects
	FMetaHumanFaceTextureSynthesizer FaceTextureSynthesizer;

	// Skin Tone Texture created from FaceTextureSynthesizer used in UI skin tone picker
	TWeakObjectPtr<class UTexture2D> SkinToneTexture;

	// Delegate handle for character instance update
	FDelegateHandle CharacterInstanceUpdatedDelegateHandle;

	// True if TryAddObjectToEdit is running
	bool bIsAddingObjectToEdit = false;

public:
	/**
	* Utility function that invokes a callback for each valid MetaHuman Character Editor Actor registered against the given MetaHuman Character
	 */
	void ForEachCharacterActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TFunction<void(TScriptInterface<class IMetaHumanCharacterEditorActorInterface>)> InFunc);

	//
	// Editing environment changes from toolbar options
	//
	UE_DEPRECATED(5.7, "Please use OnNotifyLightingEnvironmentChanged function for getting a delegate for all level related changes.")
	FOnNotifyLightingEnvironmentChanged& OnLightTonemapperChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	UE_DEPRECATED(5.7, "Please use OnNotifyLightingEnvironmentChanged function for getting a delegate for all level related changes.")
	FOnNotifyLightingEnvironmentChanged& OnLightEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	FOnNotifyLightingEnvironmentChanged& OnNotifyLightingEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnStudioLightRotationChanged& OnLightRotationChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnStudioBackgroundColorChanged& OnBackgroundColorChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/*
	* !!!!DEPRECATED!!!!
	* USE NotifyLightingEnvironmentChanged
	* Updates the Environment Lighting studio.
	* This function executes an EnvironmentUpdate delegate which has a bound function inside of an EditorToolkit.
	* It is called when change happens inside a tile view which holds lighting studio options in toolbar menu or if tonemapper option changes,
	* or if user decides to use custom lighting environment.
	*/
	UE_DEPRECATED(5.7, "Please use NotifyLightingEnvironmentChanged function for all level related changes.")
	void UpdateLightingEnvironment(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterEnvironment InLightingEnvironment) const;

	/*
	*  !!!!DEPRECATED!!!!
	* USE NotifyLightingEnvironmentChanged
	* Updates the Environment Lighting studio.
	* This function executes an EnvironmentUpdate delegate which has a bound function inside of an EditorToolkit.
	* It is called when change happens inside a tile view which holds lighting studio options in toolbar menu.
	*/
	UE_DEPRECATED(5.7, "Please use NotifyLightingEnvironmentChanged function for all level related changes.")
	void UpdateTonemapperOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInToneMapperEnabled) const;


	/*
	* Used for lighting scenario levels updates, including custom levels and post process levels, signals an update to editor toolkit
	*/
	void NotifyLightingEnvironmentChanged(TNotNull<UMetaHumanCharacter*> InCharacter) const;

	/**
	 *
	 */
	void UpdateLightRotation(TNotNull<UMetaHumanCharacter*> InCharacter, float InRotation) const;

	/**
	 * Updates the background color of the lighting environment
	 */
	void UpdateBackgroundColor(TNotNull<UMetaHumanCharacter*> InCharacter, const FLinearColor& InBackgroundColor) const;

	/*
	* Updates the Character Level of detail shown in Editor.
	*/
	void UpdateCharacterLOD(TNotNull<UMetaHumanCharacter*> InCharacter, const EMetaHumanCharacterLOD NewLODValue) const;

	/**
	 * Updates character actor groom components to always use cards instead of strands.
	 */
	void UpdateAlwaysUseHairCardsOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInAlwaysUseHairCards) const;
};
