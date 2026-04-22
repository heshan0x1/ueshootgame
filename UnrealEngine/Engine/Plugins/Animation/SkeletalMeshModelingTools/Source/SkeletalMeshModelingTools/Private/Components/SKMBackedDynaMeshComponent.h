// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"

#include "SKMBackedDynaMeshComponent.generated.h"

#define UE_API SKELETALMESHMODELINGTOOLS_API


class USkeletonModifier;


UCLASS(MinimalAPI, Transient)
class USkeletalMeshBackedDynamicMeshComponent :
	public UDynamicMeshComponent
{
	GENERATED_BODY()
public:	
	
	struct FSkeletonChangeTracker
	{
		void Init(int32 NumBones);
		void HandleSkeletonChanged(const TArray<int32> ToolBoneIndexTracker);
		int32 GetChangeCount() const;
		const TArray<int32>& GetBoneIndexTracker() const;

	private:
		// Used to update retarget settings
		TArray<int32> BoneIndexTracker;

		int32 ChangeCount = 0;
	};

	struct FMorphTargetChangeTracker
	{
		void Init(const TArray<FName> ExistingMorphTargets);
		void HandleRenameMorphTarget(FName CurrentName, FName NewName);
		void HandleRemoveMorphTarget(FName Name);
		void HandleAddMorphTarget(FName Name);
		void HandleEditMorphTarget(FName Name);
		FName GetCurrentMorphTargetName(FName OriginalName) const;
		FName GetOriginalMorphTargetName(FName CurrentName) const;
		const TSet<FName>& GetEditedMorphTargets() const;
		struct FNameInfo
		{
			FName OldName;
			FName NewName;
		};
		TArray<FNameInfo> GetCurvesToRename() const;
		TArray<FName> GetCurvesToRemove() const;
		TArray<FName> GetCurvesToAdd() const;

		TArray<FName> GetCurrentMorphTargetNames() const;
	
	private:
		TMap<FName, FName> OriginalNameToCurrentName;
		TMap<FName, FName> CurrentNameToOriginalName;
		TSet<FName> EditedMorphTargets;

	};
	

	EMeshLODIdentifier Init(USkeletalMesh* InSkeletalMesh, EMeshLODIdentifier InLOD);
	EMeshLODIdentifier GetLOD() const;
	void CommitToSkeletalMesh();
	bool IsCommitting() const;
	void DiscardChanges();

	void MarkDirty();
	void HandleSkeletonChange(USkeletonModifier* InModifier);
	void ForwardVisibilityChangeRequest(bool bInVisible);

	FName AddMorphTarget(FName InName);
	FName RenameMorphTarget(FName InOldName, FName InNewName);
	void RemoveMorphTargets(const TArray<FName>& InNames);
	TArray<FName> DuplicateMorphTargets(const TArray<FName>& InNames);
	
	void MarkMorphTargetEdited(FName InName);
	
	const FSkeletonChangeTracker& GetSkeletonChangeTracker() const {return Trackers.SkeletonChangeTracker;}
	const FMorphTargetChangeTracker& GetMorphTargetChangeTracker() const {return Trackers.MorphTargetChangeTracker;}
	const FReferenceSkeleton& GetRefSkeleton() const {return Trackers.RefSkeleton;}
	const TArray<FTransform>& GetComponentSpaceBoneTransformsRefPose() const {return Trackers.ComponentSpaceBoneTransformsRefPose;}

	bool IsSkeletonDirty() const;
	bool IsDirty() const;
	int32 GetChangeCount() const;
	

	USkeletalMesh* GetSkeletalMesh() const;
	
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRequestingVisibilityChange, bool);
	FOnRequestingVisibilityChange& GetOnRequestingVisibilityChange() {return OnRequestingVisibilityChangeDelegate;}
	
protected:
	void ResetTrackersDirect();
	FName GetAvailableMorphTargetName(FName InNewName, FName InOldName = NAME_None) const;
	EMeshLODIdentifier GetValidEditingLOD(EMeshLODIdentifier InLOD) const;

	TSharedPtr<FDynamicMesh3> GetMeshCopy();
	void MarkDirtyInternal();

	struct FTrackers
	{
		TSharedPtr<FDynamicMesh3> InitialAssetMesh;
		
		int32 ChangeCount = 0;
		
		FReferenceSkeleton RefSkeleton;
		TArray<FTransform> ComponentSpaceBoneTransformsRefPose;
		FSkeletonChangeTracker SkeletonChangeTracker;
		FMorphTargetChangeTracker MorphTargetChangeTracker;
	};

	FTrackers Trackers;

	TWeakObjectPtr<USkeletalMesh> WeakSkeletalMesh;
	EMeshLODIdentifier LOD = EMeshLODIdentifier::LOD0;
	int32 LODIndex = 0;

	bool bIsCommiting = false;
	
	FOnRequestingVisibilityChange OnRequestingVisibilityChangeDelegate;
	
	class FTrackerChange : public FToolCommandChange
	{
	public:
		FTrackers OldTrackers;
		FTrackers NewTrackers;
		FTrackerChange(USkeletalMeshBackedDynamicMeshComponent* InComponent);
		virtual FString ToString() const override;
		virtual void Apply(UObject* Object) override;
		virtual void Revert(UObject* Object) override;
		void Close(USkeletalMeshBackedDynamicMeshComponent* InComponent);	
	};
	
	// Records into current Transaction if there is one
	class FChangeScope 
	{
	public:
		FChangeScope(USkeletalMeshBackedDynamicMeshComponent* InComponent, bool bInRecordMeshChange = true);
		~FChangeScope();
		
	private:
		TUniquePtr<FTrackerChange> TrackerChange;
		TSharedPtr<FDynamicMesh3> OldMesh;
		TSharedPtr<FDynamicMesh3> NewMesh;

		USkeletalMeshBackedDynamicMeshComponent* Component = nullptr;
		bool bRecordMeshChange = false;

		friend class FTrackerChange;
	};

	class FTrackerChangeScope : public FChangeScope
	{
	public:
		FTrackerChangeScope(USkeletalMeshBackedDynamicMeshComponent* InComponent) : FChangeScope(InComponent, false) {}
	};


	
	friend class FChangeScope;
	friend class FTrackerChange;
};

#undef UE_API
