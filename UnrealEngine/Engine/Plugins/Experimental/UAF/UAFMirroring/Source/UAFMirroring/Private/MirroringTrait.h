// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/KismetMathLibrary.h"
#include "TraitCore/Trait.h"
#include "TraitCore/TraitSharedData.h"
#include "TraitInterfaces/IAttributeProvider.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/IHierarchy.h"
#include "TraitInterfaces/IUpdate.h"
#include "TraitInterfaces/IGarbageCollection.h"
#include "MirroringTraitData.h"
#include "CustomBoneIndexArray.h"

#include "MirroringTrait.generated.h"

class UMirrorDataTable;
class USkeletalMesh;

namespace UE::UAF
{
	/**
	 * Mirroring Cache
	 *
	 * Holds all precomputed data needed to mirror keyframe output efficiently, so it doesn’t have to be rebuilt every evaluation.
	 * 
	 * @see FAnimNextEvaluationMirroringTask
	 */
	struct FMirroringTraitCache
	{
		// Cached mirror indices map (invalidated on skeletal mesh or mirror data table change)
		TArray<FBoneIndexType> MeshBoneIndexToMirroredMeshBoneIndexMap;
		
		// Cached bind pose rotations (invalidated on skeletal mesh change)
		TArray<FQuat> MeshSpaceReferencePoseRotations;

		// Cached bind pose rotation corrections (invalidated on skeletal mesh change)
		TArray<FQuat> MeshSpaceReferenceRotationCorrections;
		
		// Cached mirror indices map but using FCompactPoseIndex (invalidated on skeletal mesh or mirror data table change)
		TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex> CompactPoseBoneIndexToMirroredCompactPoseBoneIndexMap;

		// Skeletal mesh used to build this cache
		TWeakObjectPtr<const USkeletalMesh> SkeletalMesh = nullptr;

		// Mirror data table used to build this cache
		TWeakObjectPtr<const UMirrorDataTable> MirrorTable = nullptr;

		/**
		 * True if the cached mirror maps were generated using exactly these assets.
		 * Any mismatch (different mesh/mirror table, size mismatch) means you must rebuild the mirror maps.
		 */
		bool AreMirrorMapsValid(const UE::UAF::FReferencePose& InReferencePose, const TWeakObjectPtr<const UMirrorDataTable>& InMirrorTable, bool bInShouldMirrorBones, bool bInShouldMirrorAttributes) const;

		/**
		 * True if the cached bind/reference-pose data matches this mesh.
		 * Any mismatch (different mesh table, size mismatch)  means you must rebuild the reference-pose arrays.
		 */
		bool IsReferencePoseDataValid(const UE::UAF::FReferencePose& InReferencePose, bool bInShouldMirrorBones) const;

		/**
		 * True if the cache was generated using exactly these assets.
		 * Any mismatch (different mesh/mirror table, size mismatch) means you must rebuild the cache.
		 */
		bool IsValid(const UE::UAF::FReferencePose& InReferencePose, const TWeakObjectPtr<const UMirrorDataTable>& InMirrorTable, bool bInShouldMirrorBones, bool bInShouldMirrorAttributes) const;

		/** Empty arrays and reset ptrs to assets */
		void Clear();
	};
	
	/**
	 * Mirroring Base Trait
	 * 
	 * A trait that can mirror an input's keyframe data.
	 */
	struct FMirroringTrait : FBaseTrait, IEvaluate, IHierarchy, IGarbageCollection
	{
		DECLARE_ANIM_TRAIT(FMirroringTrait, FBaseTrait)

		using FSharedData = FMirroringTraitSharedData;

		struct FInstanceData : FTrait::FInstanceData
		{
			// Input node to query the keyframe to be mirrored.
			FTraitPtr Input;
			
			// Defines whether to perform mirror pass (and what data table to use).
			FMirroringTraitSetupParams Setup;

			// Defines what channels will be affected during the mirror pass.
			FMirroringTraitApplyToParams ApplyTo;

			// Used to trigger inertial blends.
			bool bHasMirrorStateChanged = false;
			
			// Data that stays the same between evaluation passes.
			FMirroringTraitCache Cache;
			
			void Construct(const FExecutionContext& Context, const FTraitBinding& Binding);
			void Deconstruct(const FExecutionContext& Context, const FTraitBinding& Binding);
		};
		
		// IEvaluate impl
		virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const override;
		
		// IHierarchy impl
		virtual uint32 GetNumChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding) const override;
		virtual void GetChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding, FChildrenArray& Children) const override;
		
		// IGarbageCollection impl
		virtual void AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const override;
	};

	/**
	 * Mirroring Additive Trait
	 * 
	 * Same behaviour as FMirroringTrait, but as an additive (i.e. it only mirrors the super-trait’s output).
	 */
	struct FMirroringAdditiveTrait : FAdditiveTrait, IUpdate, IEvaluate, IGarbageCollection
	{
		DECLARE_ANIM_TRAIT(FMirroringAdditiveTrait, FAdditiveTrait)

		using FSharedData = FMirroringAdditiveTraitSharedData;

		struct FInstanceData : FTrait::FInstanceData
		{
			// Defines whether to perform mirror pass (and what data table to use).
			FMirroringTraitSetupParams Setup;

			// Defines what channels will be affected during the mirror pass.
			FMirroringTraitApplyToParams ApplyTo;
			
			// Used to trigger inertial blends.
			bool bHasMirrorStateChanged = false;
			
			// Data that stays the same between evaluation passes.
			FMirroringTraitCache Cache;

			void Construct(const FExecutionContext& Context, const FTraitBinding& Binding);
			void Deconstruct(const FExecutionContext& Context, const FTraitBinding& Binding);
		};

		// IEvaluate impl
		virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const override;

		// IGarbageCollection impl
		virtual void AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const override;
	};
	
} // namespace UE::UAF

/**
 * Mirroring Task
 * 
 * This pop the top keyframe from the VM keyframe stack, mirrors it, and pushes
 * back the result onto the stack.
 */
USTRUCT()
struct FAnimNextEvaluationMirroringTask : public FAnimNextEvaluationTask
{
	GENERATED_BODY()

	DECLARE_ANIM_EVALUATION_TASK(FAnimNextEvaluationMirroringTask)

	// Make a FAnimNextEvaluationMirroringTask task
	static FAnimNextEvaluationMirroringTask Make(const UE::UAF::FMirroringTraitSetupParams & Setup, const UE::UAF::FMirroringTraitApplyToParams & ApplyTo, UE::UAF::FMirroringTraitCache* Cache);
	
	// Settings to use when doing the mirror pass
	UPROPERTY(VisibleAnywhere, Category = "Setup")
	FUAFMirroringTraitSetupParams Setup;

	UPROPERTY(VisibleAnywhere, Category = "Apply To")
	FUAFMirroringTraitApplyToParams ApplyTo;

	// Cache for bind pose rotation, corrections, and mirror map
	UE::UAF::FMirroringTraitCache* Cache = nullptr;
	
	// Task entry point
	virtual void Execute(UE::UAF::FEvaluationVM& VM) const override;
	
private:

	// Ensures the mirror cache matches the given inputs, rebuilds if stale.
	void EnsureCache(const UE::UAF::FEvaluationVM& VM, const UE::UAF::FReferencePose& InReferencePose) const;
};