// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "ClothingAssetBase.generated.h"

class USkeletalMesh;

#define UE_API CLOTHINGSYSTEMRUNTIMEINTERFACE_API

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogClothingAsset, Log, All);

/**
 * An interface object for any clothing asset the engine can use.
 * Any clothing asset concrete object should derive from this.
 */
UCLASS(Abstract, MinimalAPI)
class UClothingAssetBase : public UObject
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	/** Binds a clothing asset submesh to a skeletal mesh section
	* @param InSkelMesh Skel mesh to bind to
	* @param InSectionIndex Section in the skel mesh to replace
	* @param InSubmeshIdx Submesh in this asset to replace section with
	* @param InAssetLodIndex Internal clothing LOD to use
	*/
	virtual bool BindToSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex, const int32 InSectionIndex, const int32 InAssetLodIndex)
	PURE_VIRTUAL(UClothingAssetBase::BindToSkeletalMesh, return false;);

	/**
	* Unbinds this clothing asset from the provided skeletal mesh, will remove all LODs
	* @param InSkelMesh skeletal mesh to remove from
	*/
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh)
	PURE_VIRTUAL(UClothingAssetBase::UnbindFromSkeletalMesh, );

	/**
	* Unbinds this clothing asset from the provided skeletal mesh
	* @param InSkelMesh skeletal mesh to remove from
	* @param InMeshLodIndex Mesh LOD to remove this asset from (could still be bound to other LODs)
	*/
	virtual void UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh, const int32 InMeshLodIndex)
	PURE_VIRTUAL(UClothingAssetBase::UnbindFromSkeletalMesh,);

	/**
	 * Update all extra LOD deformer mappings.
	 * This should be called whenever the raytracing LOD bias is changed.
	 */
	virtual void UpdateAllLODBiasMappings(USkeletalMesh* SkeletalMesh)
	PURE_VIRTUAL(UClothingAssetBase::UpdateAllLODBiasMappings,);

	/** Add a new LOD class instance. */
	UE_DEPRECATED(5.7, "Use UClothingAssetCommon::AddNewLod() instead.")
	virtual int32 AddNewLod()
	{
		return INDEX_NONE;
	}

	/**
	*	Builds the LOD transition data
	*	When we transition between LODs we skin the incoming mesh to the outgoing mesh
	*	in exactly the same way the render mesh is skinned to create a smooth swap
	*/
	UE_DEPRECATED(5.7, "Use UClothingAssetCommon::BuildLodTransitionData() instead.")
	virtual void BuildLodTransitionData() {}
#endif

#if WITH_EDITORONLY_DATA
	/**
	 * Called on the clothing asset when the base data (physical mesh, config etc.)
	 * has changed, so any intermediate generated data can be regenerated.
	 */
	UE_DEPRECATED(5.7, "Use UClothingAssetCommon::InvalidateAllCachedData() instead.")
	virtual void InvalidateAllCachedData() {}
#endif // WITH_EDITORONLY_DATA

	/** 
	 * Messages to the clothing asset that the bones in the parent mesh have
	 * possibly changed, which could invalidate the bone indices stored in the LOD
	 * data.
	 * @param InSkelMesh - The mesh to use to remap the bones
	 */
	virtual void RefreshBoneMapping(USkeletalMesh* InSkelMesh)
	PURE_VIRTUAL(UClothingAssetBase::RefreshBoneMapping, );

	/** Check the validity of this clothing data, for example for when it is dependent from an asset that isn't ready or accessible. */
	virtual bool IsValid() const
	{
		return true;
	}

	/** Check the validity of a LOD index */
	UE_DEPRECATED(5.7, "Use UClothingAssetCommon::IsValidLod() instead.")
	virtual bool IsValidLod(int32 InLodIndex) const
	{
		return false;
	}

	/** Get the number of LODs defined in the clothing asset */
	UE_DEPRECATED(5.7, "Use UClothingAssetCommon::GetNumLods() instead.")
	virtual int32 GetNumLods() const
	{
		return 0;
	}

	/** Called after all cloth assets sharing the same simulation are added or loaded */
	virtual void PostUpdateAllAssets()
	PURE_VIRTUAL(UClothingAssetBase::PostUpdateAllAssets(), );

	/** Get the guid identifying this asset */
	const FGuid& GetAssetGuid() const
	{
		return AssetGuid;
	}

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.6, "This is an old Apex file path which is no longer used and will be removed.")
	UPROPERTY()
	FString ImportedFilePath;
#endif

protected:

	/** The asset factory should have access, as it will assign the asset guid when building assets */
	friend class UClothingAssetFactory;

#if WITH_EDITOR
	/** Warn using slate's notification manager. Use this method to notify binding issues back to the user. */
	static UE_API void WarningNotification(const FText& Text);
#endif

	/** Guid to identify this asset. Will be embedded into chunks that are created using this asset */
	UPROPERTY()
	FGuid AssetGuid;
};

#undef UE_API
