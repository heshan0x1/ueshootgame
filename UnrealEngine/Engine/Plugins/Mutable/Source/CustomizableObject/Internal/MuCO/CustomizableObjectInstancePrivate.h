// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Materials/MaterialInterface.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"
#include "MuR/Instance.h"
#include "GameplayTagContainer.h"
#include "MuCO/DescriptorHash.h"
#include "UObject/Package.h"
#include "Tasks/Task.h"

#include "CustomizableObjectInstancePrivate.generated.h"

#define UE_API CUSTOMIZABLEOBJECT_API

namespace UE::Mutable::Private 
{
	class FPhysicsBody;
	class FMesh;
}

struct FMutableModelImageProperties;
struct FMutableRefSkeletalMeshData;
struct FMutableImageCacheKey;
struct FStreamableManager;
struct FStreamableHandle;
struct FGeneratedMaterial;
struct FGeneratedTexture;
class UPhysicsAsset;
class USkeleton;
class UCustomizableObjectExtension;
class UModelResources;


// Log texts
extern const FString MULTILAYER_PROJECTOR_PARAMETERS_INVALID;

	
// FParameters encoding
CUSTOMIZABLEOBJECT_API extern const FString NUM_LAYERS_PARAMETER_POSTFIX;
CUSTOMIZABLEOBJECT_API extern const FString OPACITY_PARAMETER_POSTFIX;
CUSTOMIZABLEOBJECT_API extern const FString IMAGE_PARAMETER_POSTFIX;
CUSTOMIZABLEOBJECT_API extern const FString POSE_PARAMETER_POSTFIX;


/** \param OnlyLOD: If not 0, extract and convert only one single LOD from the source image.
  * \param ExtractChannel: If different than -1, extract a single-channel image with the specified source channel data. */
CUSTOMIZABLEOBJECT_API void ConvertImage(UTexture2D* Texture, TSharedPtr<const UE::Mutable::Private::FImage> MutableImage, const FMutableModelImageProperties& Props, int32 OnlyLOD = -1, int32 ExtractChannel = -1);


/** CustomizableObject Instance flags for internal use  */
enum ECOInstanceFlags
{
	ECONone							= 0,  // Should not use the name None here.. it collides with other enum in global namespace

	// Update process
	ReuseTextures					= 1 << 3, 	// 
	ReplacePhysicsAssets			= 1 << 4,	// Merge active PhysicsAssets and replace the base physics asset

	// Update priorities
	UsedByComponent					= 1 << 5,	// If any components are using this instance, they will set flag every frame
	UsedByComponentInPlay			= 1 << 6,	// If any components are using this instance in play, they will set flag every frame
	UsedByPlayerOrNearIt			= 1 << 7,	// The instance is used by the player or is near the player, used to give more priority to its updates
	DiscardedByNumInstancesLimit	= 1 << 8,	// The instance is discarded because we exceeded the limit of instances generated 

	// Types of updates
	PendingLODsUpdate				= 1 << 9,	// Used to queue an update due to a change in LODs required by the instance
	PendingLODsDowngrade			= 1 << 10,	// Used to queue a downgrade update to reduce the number of LODs. LOD update goes from a high res level to a low res one, ex: 0 to 1 or 1 to 2
	
	// Generation
	ForceGenerateMipTail			= 1 << 13,	// If set, SkipGenerateResidentMips will be ignored and the mip tail will be generated
};

ENUM_CLASS_FLAGS(ECOInstanceFlags);


USTRUCT()
struct FReferencedPhysicsAssets
{
	GENERATED_USTRUCT_BODY();
	
	TArray<int32> PhysicsAssetToLoad;
	
	UPROPERTY(Transient)
	TArray< TObjectPtr<UPhysicsAsset> > PhysicsAssetsToMerge;

	TArray<int32> AdditionalPhysicsAssetsToLoad;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicsAsset>> AdditionalPhysicsAssets;
};


USTRUCT()
struct FReferencedSkeletons
{
	GENERATED_USTRUCT_BODY();

	// Merged skeleton if found in the cache
	UPROPERTY()
	TObjectPtr<USkeleton> Skeleton;

	UPROPERTY()
	TArray<uint16> SkeletonIds;

	UPROPERTY()
	TArray< TObjectPtr<USkeleton> > SkeletonsToMerge;
};


USTRUCT()
struct FCustomizableInstanceComponentData
{
	GENERATED_USTRUCT_BODY();

	// AnimBP data gathered for a component from its constituent meshes
	UPROPERTY(Transient, Category = CustomizableObjectInstance, editfixedsize, VisibleAnywhere)
	TMap<FName, TSoftClassPtr<UAnimInstance>> AnimSlotToBP;

	// AssetUserData gathered for a component from its constituent meshes
	UPROPERTY(Transient, Category = CustomizableObjectInstance, editfixedsize, VisibleAnywhere)
	TSet<TObjectPtr<UAssetUserData>> AssetUserDataArray;

	// Index of the resource in the StreamedResourceData array of the CustomizableObject.
	TArray<int32> StreamedResourceIndex;

#if WITH_EDITORONLY_DATA
	// Just used for mutable.EnableMutableAnimInfoDebugging command
	TArray<FString> MeshPartPaths;
#endif

	/** Skeletons required by the current generated instance. Skeletons to be loaded and merged.*/
	UPROPERTY(Transient)
	FReferencedSkeletons Skeletons;
	
	/** PhysicsAssets required by the current generated instance. PhysicsAssets to be loaded and merged.*/
	UPROPERTY(Transient)
	FReferencedPhysicsAssets PhysicsAssets;

	/** Clothing PhysicsAssets required by the current generated instance. PhysicsAssets to be loaded and merged.*/
	TArray<TPair<int32, int32>> ClothingPhysicsAssetsToStream;

	/** Array of generated MeshIds per each LOD, used to decide if the mesh should be updated or not.
	 *  Size == NumLODsAvailable
	 *  LODs without mesh will be set to the maximum value of FResourceID (Max_uint64). */
	TArray<UE::Mutable::Private::FMeshId> LastMeshIdPerLOD;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OverlayMaterial;
};

USTRUCT()
struct FAnimInstanceOverridePhysicsAsset
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	int32 PropertyIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsAsset> PhysicsAsset;
};

USTRUCT()
struct FAnimBpGeneratedPhysicsAssets
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FAnimInstanceOverridePhysicsAsset> AnimInstancePropertyIndexAndPhysicsAssets;
};


USTRUCT()
struct FExtensionInstanceData
{
	GENERATED_BODY()
 
	UPROPERTY()
	TWeakObjectPtr<const UCustomizableObjectExtension> Extension;
 
	UPROPERTY()
	FInstancedStruct Data;
};


#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FObjectInstanceTransactedDelegate, const FTransactionObjectEvent&);
#endif //WITH_EDITOR


/** Indicates the status of the generated Skeletal Mesh. */
enum class ESkeletalMeshStatus : uint8
{
	NotGenerated, // Set only when loading the Instance for the first time or after compiling. Any generation, successful or not, can not end up in this state.
	Success, // Generated successfully.
	Error // Not generated. Set only after a failed update.
};


USTRUCT()
struct FTextureReuseCacheKey
{
	GENERATED_BODY()
	
	FTextureReuseCacheKey() = default;
	FTextureReuseCacheKey(int32 InLODIndex, int32 InComponentIndex, uint32 InSurfaceIndex, int32 InImageIndex)
	{
		LODIndex = InLODIndex;
		ComponentIndex = InComponentIndex;
		SurfaceIndex = InSurfaceIndex;
		ImageIndex = InImageIndex;
	}

	friend uint32 GetTypeHash(FTextureReuseCacheKey In)
	{
		return HashCombine(GetTypeHash(In.LODIndex), GetTypeHash(In.ComponentIndex), GetTypeHash(In.SurfaceIndex), GetTypeHash(In.ImageIndex));
	}
	
	bool operator==(const FTextureReuseCacheKey& Other) const
	{
		return	LODIndex == Other.LODIndex && ComponentIndex == Other.ComponentIndex &&
				SurfaceIndex == Other.SurfaceIndex && ImageIndex == Other.ImageIndex;
	}

	uint32 SurfaceIndex = 0;
	int32 LODIndex = -1;
	int32 ComponentIndex = -1;
	int32 ImageIndex = -1;
};


UCLASS(MinimalAPI)
class UCustomizableInstancePrivate : public UObject
{
public:
	GENERATED_BODY()

	/** The generated skeletal meshes for this Instance. They may be null if the component is empty. */
	UPROPERTY(Transient, VisibleAnywhere, Category = NoCategory, meta=(DisplayName = "Meshes"))
	TMap<FName, TObjectPtr<USkeletalMesh>> SkeletalMeshes;
	
	UPROPERTY(Transient, VisibleAnywhere, Category = NoCategory, meta=(DisplayName = "Materials"))
	TArray<FGeneratedMaterial> GeneratedMaterials;

	UPROPERTY(Transient)
	TArray<FGeneratedTexture> GeneratedTextures;

	// Indices of the parameters that are relevant for the given parameter values.
	// This only gets updated if parameter decorations are generated.
	TArray<int> RelevantParameters;

	// If Texture reuse is enabled, stores which texture is being used in a particular <LODIndex, ComponentIndex, MeshSurfaceIndex, image>
	UPROPERTY(Transient)
	TMap<FTextureReuseCacheKey, TWeakObjectPtr<UTexture2D>> TextureReuseCache;
	
	// Only used in LiveUpdateMode to reuse core instances between updates and their temp data to speed up updates, but spend way more memory
	UE::Mutable::Private::FInstance::FID LiveUpdateModeInstanceID = 0;

#if WITH_EDITOR
	UE_API virtual void PostDuplicate(bool bDuplicateForPIE) override;

	UE_API void BindObjectDelegates(UCustomizableObject* CurrentCustomizableObject, UCustomizableObject* NewCustomizableObject);

	UE_API void OnPostCompile();
	UE_API void OnObjectStatusChanged(FCustomizableObjectStatus::EState Previous, FCustomizableObjectStatus::EState Next);
#endif
	
	/** Invalidates the previously generated data and retrieves information from the CObject after specific actions.
	 *  It'll be called in the PostLoad, after Compiling the CO, and after changing the CO of the Instance. */
	UE_API void InitCustomizableObjectData(const UCustomizableObject* InCustomizableObject);

	UE_API FCustomizableInstanceComponentData* GetComponentData(const FName& ComponentName);
	void ClearInstanceComponentData(const FName& ComponentName);

	ECOInstanceFlags GetCOInstanceFlags() const { return InstanceFlagsPrivate; }
	void SetCOInstanceFlags(ECOInstanceFlags FlagsToSet) { InstanceFlagsPrivate = (ECOInstanceFlags)(InstanceFlagsPrivate | FlagsToSet); }
	void ClearCOInstanceFlags(ECOInstanceFlags FlagsToClear) { InstanceFlagsPrivate = (ECOInstanceFlags)(InstanceFlagsPrivate & ~FlagsToClear); }
	bool HasCOInstanceFlags(ECOInstanceFlags FlagsToCheck) const { return (InstanceFlagsPrivate & FlagsToCheck) != 0; }

	UE_API void BuildMaterials(const TSharedRef<FUpdateContextPrivate>& Context, UCustomizableObjectInstance* Public);

	UE_API void ReuseTexture(UTexture2D* Texture, TSharedRef<FTexturePlatformData, ESPMode::ThreadSafe>& PlatformData);

	UE_API void TickUpdateCloseCustomizableObjects(UCustomizableObjectInstance& Public, FMutableInstanceUpdateMap& InOutRequestedUpdates);
	UE_API void UpdateInstanceIfNotGenerated(UCustomizableObjectInstance& Public, FMutableInstanceUpdateMap& InOutRequestedUpdates);

	// Returns true if success (?)
	UE_API bool UpdateSkeletalMesh_PostBeginUpdate0(UCustomizableObjectInstance* Instance, const TSharedRef<FUpdateContextPrivate>& Context);
	
	// Copy data generated in the mutable thread over to the instance and initializes additional data required during the update
	UE_API void PrepareForUpdate();

	// The following method is basically copied from PostEditChangeProperty and/or SkeletalMesh.cpp to be able to replicate PostEditChangeProperty without the editor
	UE_API void PostEditChangePropertyWithoutEditor();
	
	/** Calls ReleaseResources on all SkeletalMeshes generated by this instance and invalidates the generated data.
	  * It should not be called if the meshes are still in use or shared with other instances. */
	UE_API void DiscardResources();
	
	/** Set the reference SkeletalMesh, or an empty mesh, to all actors using this instance. */
	UE_API void SetReferenceSkeletalMesh() const;

	UE_API const TArray<FAnimInstanceOverridePhysicsAsset>* GetGeneratedPhysicsAssetsForAnimInstance(TSubclassOf<UAnimInstance> AnimInstance) const;

	/** */
#if WITH_EDITORONLY_DATA
	UE_API void RegenerateImportedModels(const TSharedRef<FUpdateContextPrivate>& OperationData);
#endif
	
	// 
	UE_API FCustomizableInstanceComponentData* GetComponentData(FCustomizableObjectComponentIndex ObjectComponentIndex);
	UE_API const FCustomizableInstanceComponentData* GetComponentData(FCustomizableObjectComponentIndex ObjectComponentIndex) const;
	
	// Create a transient texture and add it to the TextureTrackerArray
	UE_API UTexture2D* CreateTexture(const FString& TextureName);

	UE_API void InvalidateGeneratedData();

	UE_API bool LoadParametersFromProfile(int32 ProfileIndex);
	
	UE_API bool SaveParametersToProfile(int32 ProfileIndex);
	
	UE_API bool MigrateProfileParametersToCurrentInstance(int32 ProfileIndex);

	UE_API void SetSelectedParameterProfileDirty();

	UE_API bool IsSelectedParameterProfileDirty() const;

	UE_API int32 GetState() const;

	UE_API void SetState(int32 InState);

	UE_API FCustomizableObjectInstanceDescriptor& GetDescriptor() const;

	UE_API void SetDescriptor(const FCustomizableObjectInstanceDescriptor& InDescriptor);

	/** Return true if the instance is not locked and if it's compiled. */
	UE_API bool CanUpdateInstance() const;

	/** Finds in IntParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UE_API int32 FindIntParameterNameIndex(const FString& ParamName) const;

	/** Finds in FloatParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UE_API int32 FindFloatParameterNameIndex(const FString& ParamName) const;

	/** Finds in BoolParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UE_API int32 FindBoolParameterNameIndex(const FString& ParamName) const;

	/** Finds in VectorParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UE_API int32 FindVectorParameterNameIndex(const FString& ParamName) const;

	/** Finds in ProjectorParameters a parameter with name ParamName, returns the index if found, -1 otherwise. */
	UE_API int32 FindProjectorParameterNameIndex(const FString& ParamName) const;
	
	UE_API UCustomizableObjectInstance* GetPublic() const;

	// If any components are using this instance, they will store the min of their distances to the player here every frame for LOD purposes
	float MinSquareDistFromComponentToPlayer = FLT_MAX;
	float LastMinSquareDistFromComponentToPlayer = FLT_MAX; // The same as the previous dist for last frame
	
	// To be indexed with object component index
	UPROPERTY(Transient)
	TArray<FCustomizableInstanceComponentData> ComponentsData;

	UPROPERTY(Transient)
	TArray< TObjectPtr<UMaterialInterface> > ReferencedMaterials;

	// Converts a ReferencedMaterials index from the CustomizableObject to an index in the ReferencedMaterials in the Instance
	TMap<int32, uint32> ObjectToInstanceIndexMap;
	
	UPROPERTY(Transient)
	TArray< TObjectPtr<UPhysicsAsset> > ClothingPhysicsAssets;

	// To keep loaded AnimBPs referenced and prevent GC
	UPROPERTY(Transient, Category = Animation, editfixedsize, VisibleAnywhere)
	TArray<TSubclassOf<UAnimInstance>> GatheredAnimBPs;

	UPROPERTY(Transient, Category = Animation, editfixedsize, VisibleAnywhere)
	FGameplayTagContainer AnimBPGameplayTags;

	UPROPERTY(Transient, Category = Animation, editfixedsize, VisibleAnywhere)
	TMap<TSubclassOf<UAnimInstance>, FAnimBpGeneratedPhysicsAssets> AnimBpPhysicsAssets;

	UPROPERTY(Transient)
	TArray<FExtensionInstanceData> ExtensionInstanceData;
	
	// The pass-through assets that will be loaded during an update
	TArray<TSoftObjectPtr<const UTexture>> PassThroughTexturesToLoad;
	TArray<TSoftObjectPtr<const UStreamableRenderAsset>> PassThroughMeshesToLoad;

private:
	ECOInstanceFlags InstanceFlagsPrivate = ECOInstanceFlags::ECONone;

public:
	/** Copy of the descriptor of the latest successful update. */
	UPROPERTY(Transient)
	FCustomizableObjectInstanceDescriptor CommittedDescriptor;

	/** Hash of the descriptor copy of the latest successful update. */
	FDescriptorHash CommittedDescriptorHash;
	
	/** Status of the generated Skeletal Mesh. Not to be confused with the Update Result. */
	ESkeletalMeshStatus SkeletalMeshStatus = ESkeletalMeshStatus::NotGenerated;

	TMap<FString, bool> ParamNameToExpandedMap; // Used to check whether a mutable param is expanded in the editor to show its child params

	bool bShowOnlyRuntimeParameters = true;
	bool bShowOnlyRelevantParameters = true;
	bool bShowUISections = false;
	bool bShowUIThumbnails = false;

	/** Automatic update required.
	 * Set to true when a Customizable Object Instance Usage requires an automatic update (e.g., component reattached). */
	bool bAutomaticUpdateRequired = false;
	
	// TEMP VARIABLE to check the Min desired LODs for this instance
	TWeakObjectPtr<UCustomizableObjectInstanceUsage> NearestToActor;
	TWeakObjectPtr<const AActor> NearestToViewCenter;

#if WITH_EDITOR
	/** Profile index the instance parameters are in and if the profile needs to be refreshed */
	int32 SelectedProfileIndex = INDEX_NONE;
	bool bSelectedProfileDirty = false;
#endif 
	
#if WITH_EDITORONLY_DATA
	/** Preview Instance Properties search box filter. Saved here to avoid losing the text during UI refreshes. */
	FText ParametersSearchFilter;
#endif

#if WITH_EDITOR
	/** Delegate called when the Instance has been transacted */
	FObjectInstanceTransactedDelegate OnInstanceTransactedDelegate;
#endif // WITH_EDITOR
};


UE_API UPhysicsAsset* GetOrBuildMainPhysicsAsset(const TSharedRef<FUpdateContextPrivate>& Context, TObjectPtr<UPhysicsAsset> TamplateAsset, const UE::Mutable::Private::FPhysicsBody* PhysicsBody, bool bDisableCollisionBetweenAssets, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex);

UE_API USkeleton* MergeSkeletons(UCustomizableObject& CustomizableObject, FCustomizableInstanceComponentData& ObjectComponentIndex, bool& bOutCreatedNewSkeleton);

UE_API void InitSkeletalMeshData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh* SkeletalMesh, const FMutableRefSkeletalMeshData& RefSkeletalMeshData, const UCustomizableObject& CustomizableObject, FCustomizableObjectComponentIndex ObjectComponentIndex);

UE_API void BuildOrCopyClothingData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh* SkeletalMesh, const UModelResources& CustomizableObjectInstance, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex, const TArray<TObjectPtr<UPhysicsAsset>>& ClothingPhysicsAssets);

UE_API bool BuildSkeletonData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh& SkeletalMesh, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex);

UE_API void BuildMeshSockets(USkeletalMesh* SkeletalMesh, const UModelResources& ModelResources, const FMutableRefSkeletalMeshData& RefSkeletalMeshData, TSharedPtr<const UE::Mutable::Private::FMesh> MutableMesh);

UE_API bool BuildOrCopyRenderData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh* SkeletalMesh, UCustomizableObjectInstance* CustomizableObjectInstance, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex);

UE_API void BuildOrCopyElementData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh* SkeletalMesh, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex);

UE_API void BuildOrCopyMorphTargetsData(const TSharedRef<FUpdateContextPrivate>& Context, USkeletalMesh* SkeletalMesh, FCustomizableObjectInstanceComponentIndex InstanceComponentIndex);
	
UE_API bool DoComponentsNeedUpdate(const TSharedRef<FUpdateContextPrivate>& Context, bool& bOutEmptyMesh);

UE_API UE::Tasks::FTask LoadAdditionalAssetsAndData(const TSharedRef<FUpdateContextPrivate>& Context);

UE_API void AdditionalAssetsAsyncLoaded(TSharedRef<FUpdateContextPrivate> Context, UE::Tasks::FTaskEvent Event);


#undef UE_API
