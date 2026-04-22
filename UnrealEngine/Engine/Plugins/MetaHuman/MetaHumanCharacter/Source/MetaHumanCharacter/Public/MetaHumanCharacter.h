// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Serialization/EditorBulkData.h"
#include "Misc/EnumRange.h"
#include "ImageCore.h"
#include "Misc/ObjectThumbnail.h"

#include "MetaHumanCharacterAssemblySettings.h"
#include "MetaHumanCharacterEyes.h"
#include "MetaHumanCharacterMakeup.h"
#include "MetaHumanCharacterMaterialSet.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanCharacterSkin.h"
#include "MetaHumanCharacterTeeth.h"
#include "MetaHumanCharacterViewport.h"

#include "MetaHumanCharacter.generated.h"

class UMetaHumanCollection;
class UMetaHumanCollectionPipeline;

/**
* The rigging state of the Character
*/
enum class EMetaHumanCharacterRigState : uint8
{
	Unrigged = 0,
	RigPending,
	Rigged
};

/**
 * Configures single section of the wardrobe asset view.
 */
USTRUCT()
struct METAHUMANCHARACTER_API FMetaHumanCharacterAssetsSection
{
	GENERATED_BODY()

	/** Long package directory name where to look for the assets */
	UPROPERTY(EditAnywhere, Category = "Section", meta = (LongPackageName))
	FDirectoryPath ContentDirectoryToMonitor;

	/** Palette slot to target when the asset from this section is added. */
	UPROPERTY(EditAnywhere, Category = "Section")
	FName SlotName;

	/** Specifies the list of classes to look for in the given directory */
	UPROPERTY(EditAnywhere, Category = "Section")
	TArray<TSubclassOf<UObject>> ClassesToFilter;

	/** True if this section should be considered a pure virtual folder */
	UPROPERTY()
	bool bPureVirtual = false;

	bool operator==(const FMetaHumanCharacterAssetsSection& Other) const
	{
		return 
			ContentDirectoryToMonitor.Path == Other.ContentDirectoryToMonitor.Path && 
			SlotName == Other.SlotName &&
			ClassesToFilter == Other.ClassesToFilter;
	}
};

USTRUCT()
struct METAHUMANCHARACTER_API FMetaHumanCharacterWardrobeIndividualAssets
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Section")
	TArray<TSoftObjectPtr<class UMetaHumanWardrobeItem>> Items;
};

USTRUCT()
struct METAHUMANCHARACTER_API FMetaHumanCharacterIndividualAssets
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Section")
	TArray<TSoftObjectPtr<UMetaHumanCharacter>> Characters;
};

UENUM()
enum class EMetaHumanCharacterTemplateType : uint8
{
	MetaHuman,
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFaceEvaluationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Global Delta", Category = "Face", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float GlobalDelta = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Texture Position Offset", Category = "Face", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float HighFrequencyDelta = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Head Scale", Category = "Face", meta = (UIMin = "0.8", UIMax = "1.3", ClampMin = "0.8", ClampMax = "1.3"))
	float HeadScale = 1.0f;

	bool operator==(const FMetaHumanCharacterFaceEvaluationSettings& InOther) const
	{
		return GlobalDelta == InOther.GlobalDelta &&
			HighFrequencyDelta == InOther.HighFrequencyDelta &&
			HeadScale == InOther.HeadScale;
	}

	bool operator!=(const FMetaHumanCharacterFaceEvaluationSettings& InOther) const
	{
		return !(*this == InOther);
	}
};


/**
 * Struct used to serialize information about a synthesized texture
 */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterTextureInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Textures")
	int32 SizeX = 0;

	UPROPERTY(VisibleAnywhere, Category = "Textures")
	int32 SizeY = 0;

	UPROPERTY(VisibleAnywhere, Category = "Textures")
	int32 NumSlices = 0;

	UPROPERTY(VisibleAnywhere, Category = "Textures")
	uint8 Format = ERawImageFormat::BGRA8;

	UPROPERTY(VisibleAnywhere, Category = "Textures")
	uint8 GammaSpace = (uint8) EGammaSpace::sRGB;

	void Init(const FImageInfo& InImageInfo)
	{
		SizeX = InImageInfo.SizeX;
		SizeY = InImageInfo.SizeY;
		NumSlices = InImageInfo.NumSlices;
		Format = InImageInfo.Format;
		GammaSpace = (uint8) InImageInfo.GammaSpace;
	}

	FImage GetBlankImage() const
	{
		FImage Result;
		Result.Init(ToImageInfo());

		return Result;
	}

	FImageInfo ToImageInfo() const
	{
		return FImageInfo(SizeX, SizeY, NumSlices, (ERawImageFormat::Type) Format, (EGammaSpace) GammaSpace);
	}
};

USTRUCT(BlueprintType)
struct METAHUMANCHARACTER_API FMetaHumanCharacterHeadModelSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Eyelashes", Category = "Eyelashes", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyelashesProperties Eyelashes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Teeth", Category = "Teeth", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterTeethProperties Teeth;
};



/**
 * Used by thumbnail system to generate additional thumbnails (e.g. face, body)
 * and store them inside the character package.
 */
UCLASS()
class METAHUMANCHARACTER_API UMetaHumanCharacterThumbnailAux : public UObject
{
	GENERATED_BODY()
};

/**
 * Camera framing positions for taking character's thumbnail.
 */
UENUM()
enum class EMetaHumanCharacterThumbnailCameraPosition : uint8
{
	Face,
	Body,
	Character_Body,
	Character_Face
};

/**
 * The MetaHuman Character Asset holds all the information required build a MetaHuman.
 * Any data that needs to be serialized for a MetaHuman should be stored in this class
 * This class relies on the UMetaHumanCharacterEditorSubsystem to have its properties
 * initialized and its basically a container for data associated with a MetaHuman
 */
UCLASS(BlueprintType)
class METAHUMANCHARACTER_API UMetaHumanCharacter : public UObject
{
	GENERATED_BODY()

public:

	UMetaHumanCharacter();

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
#endif // WITH_EDITOR
	virtual void Serialize(FArchive& InAr) override;
	//~End UObject interface

	/**
	 * Returns true if the character is in a valid state, meaning all of its components
	 * are properly initialized. Call UMetaHumanCharacterEditorSubsystem::InitializeMetaHumanCharacter
	 * to make sure the character is in a valid state
	 */
	bool IsCharacterValid() const;

	/**
	 * Stores Face State data in a compressed buffer
	 */
	void SetFaceStateData(const FSharedBuffer& InFaceStateData);

	/**
	 * Retrieves the Face State data from the internal bulk data
	 */
	[[nodiscard]] FSharedBuffer GetFaceStateData() const;

	/**
	 * Stores Face DNA in a compressed buffer. 
	 */
	void SetFaceDNABuffer(TConstArrayView<uint8> InFaceDNABuffer, bool bInHasFaceDNABlendshapes);

	/**
	 * Returns true if the character has a face DNA stored in it
	 */
	bool HasFaceDNA() const;

	/**
	 * Returns a buffer with the Face DNA from the internal bulk data
	 */
	[[nodiscard]] TArray<uint8> GetFaceDNABuffer() const;

	/**
	 * Returns true if the character has blendshapes in the attached face DNA.
	 */
	bool HasFaceDNABlendshapes() const;

	/**
	 * Stores the Body State data in a compressed buffer
	 */
	void SetBodyStateData(const FSharedBuffer& InBodyStateData);

	/**
	 * Retrieves the Body State data from the internal bulk data
	 */
	[[nodiscard]] FSharedBuffer GetBodyStateData() const;

	/**
	 * Stores Body DNA in a compressed buffer
	 */
	void SetBodyDNABuffer(TConstArrayView<uint8> InBodyDNABuffer);

	/**
	 * Returns true if the character has a body DNA stored in it
	 */
	bool HasBodyDNA() const;

	/**
	 * Returns a buffer with the Body DNA from the internal bulk data
	 */
	[[nodiscard]] TArray<uint8> GetBodyDNABuffer() const;
	
	/**
	 * Returns true if the character has any synthesized textures stored in it
	 */
	bool HasSynthesizedTextures() const;

	/**
	 * Mark the character as having high resolution textures which can be used to
	 * prevent it from being overridden
	 */
	void SetHasHighResolutionTextures(bool bInHasHighResolutionTextures);

	/**
	 * Returns true if the character was marked as having high resolution textures
	 */
	bool HasHighResolutionTextures() const;

	/**
	 * Stores face texture data to be serialized
	 */
	void StoreSynthesizedFaceTexture(EFaceTextureType InTextureType, const FImage& InTextureData);

	/**
	 * Gets the synthesized face texture resolution.
	 */
	FInt32Point GetSynthesizedFaceTexturesResolution(EFaceTextureType InFaceTextureType) const;

	/**
	 * @brief Returns true if textures needs to be download to match the desired texture sources resolutions.
	 */
	bool NeedsToDownloadTextureSources() const;

	/**
	 * Gets the map of valid face textures. A texture is considered valid if its type is being referenced
	 * in SynthesizedFaceTexturesInfo
	 */
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>> GetValidFaceTextures() const;

	/**
	 * Stores high res body texture data to be serialized
	 */
	void StoreHighResBodyTexture(EBodyTextureType InTextureType, const FImage& InTextureData);

	/**
	 * Resets the bulk data for any texture types that are missing texture infos
	 */
	void ResetUnreferencedHighResTextureData();

	/**
	 * Removes all textures stored in character
	 */
	void RemoveAllTextures();

	/**
	 * Gets a future that can be used to obtain the actual face texture data
	 */
	[[nodiscard]] TFuture<FSharedBuffer> GetSynthesizedFaceTextureDataAsync(EFaceTextureType InTextureType) const;

	/**
	 * Gets a future that can be used to obtain the actual body texture data
	 */
	[[nodiscard]] TFuture<FSharedBuffer> GetHighResBodyTextureDataAsync(EBodyTextureType InTextureType) const;

	/**
	 * Gets the synthesized body texture resolution.
	 */
	FInt32Point GetSynthesizedBodyTexturesResolution(EBodyTextureType InBodyTextureType) const;


	/** Gets the Character's internal Collection */
	[[nodiscard]] TObjectPtr<UMetaHumanCollection> GetMutableInternalCollection();
	[[nodiscard]] const TObjectPtr<UMetaHumanCollection> GetInternalCollection() const;
	[[nodiscard]] FMetaHumanPaletteItemKey GetInternalCollectionKey() const;

public:

	// The character type used to load the appropriate identity template model
	UPROPERTY(VisibleAnywhere, DisplayName = "Template Type", Category = "Template")
	EMetaHumanCharacterTemplateType TemplateType = EMetaHumanCharacterTemplateType::MetaHuman;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Face Evaluation Settings", Category = "Face")
	FMetaHumanCharacterFaceEvaluationSettings FaceEvaluationSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Head Model Settings", Category = "Head")
	FMetaHumanCharacterHeadModelSettings HeadModelSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Skin Settings", Category = "Skin")
	FMetaHumanCharacterSkinSettings SkinSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Eyes Settings", Category = "Eyes")
	FMetaHumanCharacterEyesSettings EyesSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Template Type", Category = "Makeup")
	FMetaHumanCharacterMakeupSettings MakeupSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Has High Resolution Textures", Category = "Textures", AssetRegistrySearchable)
	bool bHasHighResolutionTextures = false;

	// Fixed body types are either imported from dna as a whole rig, or a fixed compatibility body
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Fixed Body Type", Category = "Body", AssetRegistrySearchable)
	bool bFixedBodyType = false;

	// Information about each of the face textures used to build the UTexture assets when the character is loaded
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Synthesized Face Textures Info", Category = "Textures|Face")
	TMap<EFaceTextureType, FMetaHumanCharacterTextureInfo> SynthesizedFaceTexturesInfo;

	// Transient face textures created from the data stored in SynthesizedFaceTexturesData
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Synthesized Face Textures", Category = "Textures|Face", Transient)
	TMap<EFaceTextureType, TObjectPtr<class UTexture2D>> SynthesizedFaceTextures;

	// Information about each of the high res body textures used to build the UTexture assets when the character is loaded
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "High Res Body Textures Info", Category = "Textures|Body")
	TMap<EBodyTextureType, FMetaHumanCharacterTextureInfo> HighResBodyTexturesInfo;

	// Transient body textures, can be created from the data stored in HighResBodyTexturesData
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, DisplayName = "Body Textures", Category = "Textures|Body", Transient)
	TMap<EBodyTextureType, TObjectPtr<class UTexture2D>> BodyTextures;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Viewport Settings", Category = "Lighting")
	FMetaHumanCharacterViewportSettings ViewportSettings;

#if WITH_EDITORONLY_DATA
	/** Serialized preview material, so that the editor can load the last used one */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, DisplayName = "Preview Material Type", Category = "Skin")
	EMetaHumanCharacterSkinPreviewMaterial PreviewMaterialType = EMetaHumanCharacterSkinPreviewMaterial::Default;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, AdvancedDisplay, DisplayName = "Thumbnail Info", Category = "Thumbnail")
	TObjectPtr<class UThumbnailInfo> ThumbnailInfo;

	/** Character defined wardrobe paths */
	UPROPERTY(VisibleAnywhere, DisplayName = "Wardrobe Paths", Category = "Wardrobe")
	TArray<FMetaHumanCharacterAssetsSection> WardrobePaths;

	/** Wardrobe individual assets per slot name */
	UPROPERTY(VisibleAnywhere, DisplayName = "Wardrobe Individual Assets", Category = "Wardrobe")
	TMap<FName, FMetaHumanCharacterWardrobeIndividualAssets> WardrobeIndividualAssets;

	/** Character individual assets for blend tool and presets library */
	UPROPERTY(VisibleAnywhere, DisplayName = "Character Individual Assets", Category = "Pipeline")
	TMap<FName, FMetaHumanCharacterIndividualAssets> CharacterIndividualAssets;

	/** 
	 * A list of Collection pipelines that have been instanced for this character, used to track pipeline properties.
	 * There should be only a single instance of a pipeline class. Stored in a map for convenience.
	 */
	UPROPERTY(VisibleAnywhere, DisplayName = "Pipelines Per Class", Category = "Pipeline")
	TMap<TSubclassOf<UMetaHumanCollectionPipeline>, TObjectPtr<UMetaHumanCollectionPipeline>> PipelinesPerClass;

	UPROPERTY(VisibleAnywhere, Category = "Pipeline")
	FMetaHumanCharacterAssemblySettings AssemblySettings;
#endif

#if WITH_EDITOR
	/** Callback when wardrobe settings changes in editor */
	DECLARE_MULTICAST_DELEGATE(FOnWardrobePathsChanged);
	FOnWardrobePathsChanged OnWardrobePathsChanged;

	/** Callback when rigging state changes in editor */
	DECLARE_MULTICAST_DELEGATE(FOnRiggingStateChanged);
	FOnRiggingStateChanged OnRiggingStateChanged;

	/** Callback when animation is reinitialized and anim data needs a refresh */
	DECLARE_MULTICAST_DELEGATE(FOnAnimationReinitialized);
	FOnAnimationReinitialized OnAnimationReinitialized;

	/**
	 * Notifies that the rigging state of the character changed
	 */
	void NotifyRiggingStateChanged() const;

	/** Generates a full object path from the character object path and camera position to be used in the package thumbnail map. */
	static FName GetThumbnailPathInPackage(const FString& InCharacterAssetPath, EMetaHumanCharacterThumbnailCameraPosition InThumbnailPosition);

private:
	/**
	 * Ensures the internal Collection is correctly set up to build this Character.
	 * 
	 * Should be called when the Collection is initialized and any time the Collection's Character 
	 * slot may have been modified.
	 */
	void ConfigureCollection();
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterThumbnailAux> ThumbnailAux_CharacterBody;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterThumbnailAux> ThumbnailAux_Face;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterThumbnailAux> ThumbnailAux_Body;
#endif

	/**
	 * The Character's built-in palette that is used for the build. Determines which build pipeline to use
	 * and contains all of the prepared assets that will be built for the platform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, DisplayName = "Internal Palette (build)", Category = "Pipeline", meta=(AllowPrivateAccess))
	TObjectPtr<UMetaHumanCollection> InternalCollection;

	UPROPERTY()
	FMetaHumanPaletteItemKey InternalCollectionKey;

	// Stores the Character Face State
	UE::Serialization::FEditorBulkData FaceStateBulkData;

	// Stores the Character Face DNA (optional)
	UE::Serialization::FEditorBulkData FaceDNABulkData;

	// Stores whether the face DNA contains blendshapes
	UPROPERTY(VisibleAnywhere, DisplayName = "Has Face DNA Blendshapes", Category = "DNA", AssetRegistrySearchable)
	bool bHasFaceDNABlendshapes = false;

	// Stores the Character Body State
	UE::Serialization::FEditorBulkData BodyStateBulkData;

	// Stores the Character Body DNA (optional)
	UE::Serialization::FEditorBulkData BodyDNABulkData;

	// Stores the Synthesized Face Textures data
	TSortedMap<EFaceTextureType, UE::Serialization::FEditorBulkData> SynthesizedFaceTexturesData;
	// Stores the high res body Textures data
	TSortedMap<EBodyTextureType, UE::Serialization::FEditorBulkData> HighResBodyTexturesData;
};