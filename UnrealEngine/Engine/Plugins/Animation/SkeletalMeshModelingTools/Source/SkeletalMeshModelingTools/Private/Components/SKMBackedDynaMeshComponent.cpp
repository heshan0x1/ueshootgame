// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/SKMBackedDynaMeshComponent.h"

#include "DynamicMeshToMeshDescription.h"
#include "LODUtilities.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ModelingToolTargetEditorOnlyUtil.h"
#include "ModelingToolTargetUtil.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletonModifier.h"
#include "ScopedTransaction.h"
#include "Misc/ITransaction.h"
#include "SkeletalMeshEditorSubsystem.h"
#include "ToolTargets/SkeletalMeshToolTarget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SKMBackedDynaMeshComponent)

#define LOCTEXT_NAMESPACE "SKMBackedDynaMeshComponent"

void USkeletalMeshBackedDynamicMeshComponent::FSkeletonChangeTracker::Init(int32 NumBones)
{
	BoneIndexTracker.Reserve(NumBones);
	for (int32 Index = 0; Index < NumBones; Index++)
	{
		BoneIndexTracker.Add(Index);
	}

	ChangeCount = 0;
}

void USkeletalMeshBackedDynamicMeshComponent::FSkeletonChangeTracker::HandleSkeletonChanged(const TArray<int32> ToolBoneIndexTracker)
{
	for (int32 BoneIndex = 0; BoneIndex < BoneIndexTracker.Num(); BoneIndex++)
	{
		int32 PreviousNewBoneIndex = BoneIndexTracker[BoneIndex];
		if (ToolBoneIndexTracker.IsValidIndex(PreviousNewBoneIndex))
		{
			int32 NewBoneIndex = ToolBoneIndexTracker[PreviousNewBoneIndex];
			BoneIndexTracker[BoneIndex] = NewBoneIndex;
		}
	}

	ChangeCount++;
}

int32 USkeletalMeshBackedDynamicMeshComponent::FSkeletonChangeTracker::GetChangeCount() const
{
	return ChangeCount;
}

const TArray<int32>& USkeletalMeshBackedDynamicMeshComponent::FSkeletonChangeTracker::GetBoneIndexTracker() const
{
	return BoneIndexTracker;
}

void USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::Init(const TArray<FName> ExistingMorphTargets)
{
	OriginalNameToCurrentName.Reset();
	CurrentNameToOriginalName.Reset();
	for (const FName& MorphTarget : ExistingMorphTargets)
	{
		OriginalNameToCurrentName.Emplace(MorphTarget, MorphTarget);
		CurrentNameToOriginalName.Emplace(MorphTarget, MorphTarget);
	}
}

void USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::HandleRenameMorphTarget(FName CurrentName, FName NewName)
{
	FName OriginalName = CurrentNameToOriginalName[CurrentName];

	if (OriginalName != NAME_None)
	{
		OriginalNameToCurrentName[OriginalName] = NewName;
	}

	CurrentNameToOriginalName.Remove(CurrentName);
	CurrentNameToOriginalName.Emplace(NewName, OriginalName);

	if (EditedMorphTargets.Contains(CurrentName))
	{
		EditedMorphTargets.Remove(CurrentName);
		EditedMorphTargets.Add(NewName);
	}
}

void USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::HandleRemoveMorphTarget(FName Name)
{
	FName OriginalName = CurrentNameToOriginalName[Name];
	if (OriginalName != NAME_None)
	{
		OriginalNameToCurrentName[OriginalName] = NAME_None;
	}

	CurrentNameToOriginalName.Remove(Name);
	
	EditedMorphTargets.Remove(Name);
}

void USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::HandleAddMorphTarget(FName Name)
{
	CurrentNameToOriginalName.Emplace(Name, NAME_None);
}

void USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::HandleEditMorphTarget(FName Name)
{
	EditedMorphTargets.Add(Name);
}

FName USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetCurrentMorphTargetName(FName OriginalName) const
{
	if (const FName* CurrentName = OriginalNameToCurrentName.Find(OriginalName))
	{
		return *CurrentName;
	}

	return NAME_None;
}

FName USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetOriginalMorphTargetName(FName CurrentName) const
{
	if (const FName* OriginalName = CurrentNameToOriginalName.Find(CurrentName))
	{
		return *OriginalName;
	}

	return NAME_None;
}

const TSet<FName>& USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetEditedMorphTargets() const
{
	return EditedMorphTargets;
}

TArray<USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::FNameInfo> USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetCurvesToRename() const
{
	TArray<FNameInfo> Renamed;
	for (const TPair<FName, FName>& OriginalToCurrent : OriginalNameToCurrentName)
	{
		if (OriginalToCurrent.Value != OriginalToCurrent.Key && OriginalToCurrent.Value != NAME_None)
		{
			FNameInfo Info;
			Info.OldName = OriginalToCurrent.Key;
			Info.NewName = OriginalToCurrent.Value;
			Renamed.Add(Info);
		}
	}

	return Renamed;
}

TArray<FName> USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetCurvesToRemove() const
{
	TArray<FName> Removed;
	for (const TPair<FName, FName>& OriginalToCurrent : OriginalNameToCurrentName)
	{
		if (OriginalToCurrent.Value == NAME_None)
		{
			Removed.Add(OriginalToCurrent.Key);
		}
	}

	return Removed;
}

TArray<FName> USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetCurvesToAdd() const
{
	TArray<FName> Added;	
	for (const TPair<FName, FName>& CurrentToOriginal : CurrentNameToOriginalName)
	{
		if (CurrentToOriginal.Value == NAME_None)
		{
			Added.Add(CurrentToOriginal.Key);
		}
	}
	
	return Added;
}

TArray<FName> USkeletalMeshBackedDynamicMeshComponent::FMorphTargetChangeTracker::GetCurrentMorphTargetNames() const
{
	TArray<FName> CurrentMorphTargetNames;
	CurrentNameToOriginalName.GenerateKeyArray(CurrentMorphTargetNames);
	return CurrentMorphTargetNames;
}

EMeshLODIdentifier USkeletalMeshBackedDynamicMeshComponent::Init(USkeletalMesh* InSkeletalMesh, EMeshLODIdentifier InLOD)
{
	WeakSkeletalMesh = InSkeletalMesh;
	LOD = GetValidEditingLOD(InLOD);
	LODIndex = static_cast<int32>(InLOD);

	// Mesh
	FMeshDescription* MeshDescription = InSkeletalMesh->GetMeshDescription(LODIndex);
	FDynamicMesh3 LODMesh;
	FMeshDescriptionToDynamicMesh Converter;
	constexpr bool bWantTangents = true;
	Converter.bVIDsFromNonManifoldMeshDescriptionAttr = true;
	Converter.Convert(MeshDescription, LODMesh, bWantTangents);

	SetMesh(MoveTemp(LODMesh));

	// Additional Trackers
	ResetTrackersDirect();

	return static_cast<EMeshLODIdentifier>(LODIndex);
}

EMeshLODIdentifier USkeletalMeshBackedDynamicMeshComponent::GetLOD() const
{
	return LOD;
}

void USkeletalMeshBackedDynamicMeshComponent::CommitToSkeletalMesh()
{
	if (GetChangeCount() == 0)
	{
		return;
	}

	if (bIsCommiting)
	{
		return;
	}

	TGuardValue<bool> CommitGuard(bIsCommiting, true);
	
	USkeletalMesh* SkeletalMesh = WeakSkeletalMesh.Get();

	TArray<FName> OldEngineGeneratedMorphTargets;
	USkeletalMeshEditorSubsystem::GetMorphTargetsGeneratedByEngine(SkeletalMesh, OldEngineGeneratedMorphTargets);
	
	FScopedTransaction Transaction(LOCTEXT("ApplyChangesToAsset","Apply Changes To Skeletal Mesh"));

	if (GetSkeletonChangeTracker().GetChangeCount() != 0)
	{
		USkeletonModifier* SkeletonModifier = NewObject<USkeletonModifier>();
		SkeletonModifier->SetSkeletalMesh(SkeletalMesh);

		SkeletonModifier->ExternalUpdate(GetRefSkeleton(), GetSkeletonChangeTracker().GetBoneIndexTracker());
		SkeletonModifier->CommitSkeletonToSkeletalMesh();
	}

	USkeletalMeshToolTargetFactory* LocalFactory = NewObject<USkeletalMeshToolTargetFactory>();
	LocalFactory->SetActiveEditingLOD(GetLOD());
	USkeletalMeshToolTarget* LocalTarget = CastChecked<USkeletalMeshToolTarget>(LocalFactory->BuildTarget(SkeletalMesh, {}));

	LocalTarget->CommitDynamicMesh(*GetMesh());

	// Make sure morph targets are marked as engine generated correctly
	// As soon as a morph is edited in engine, we want to mark it such that reimports in the future don't overwrite our edits.
	{
		TArray<FString> NewEngineGeneratedMorphTargets;
	
		for (const FName& OldEngineGeneratedMorphTarget : OldEngineGeneratedMorphTargets)
		{
			FName CurrentName = GetMorphTargetChangeTracker().GetCurrentMorphTargetName(OldEngineGeneratedMorphTarget);
			if (CurrentName != NAME_None)
			{
				NewEngineGeneratedMorphTargets.AddUnique(CurrentName.ToString());
				USkeletalMeshEditorSubsystem::SetMorphTargetsToGeneratedByEngine(SkeletalMesh, {CurrentName.ToString()});
			}
		}
	
		for (const FName& EditedMorphTarget : GetMorphTargetChangeTracker().GetEditedMorphTargets())
		{
			NewEngineGeneratedMorphTargets.AddUnique(EditedMorphTarget.ToString());	
		}
	
		USkeletalMeshEditorSubsystem::SetMorphTargetsToGeneratedByEngine(SkeletalMesh, NewEngineGeneratedMorphTargets);
	}

	// Also change related curves
	{
		
		UAnimCurveMetaData* AnimCurveMetaData = SkeletalMesh->GetAssetUserData<UAnimCurveMetaData>();
		if (AnimCurveMetaData == nullptr)
		{
			AnimCurveMetaData = NewObject<UAnimCurveMetaData>(SkeletalMesh, NAME_None, RF_Transactional);
			SkeletalMesh->AddAssetUserData(AnimCurveMetaData);
		}

		for (const FMorphTargetChangeTracker::FNameInfo& NameInfo : GetMorphTargetChangeTracker().GetCurvesToRename())
		{
			// Only rename if we have a morph flag set
			if (FCurveMetaData* CurveMetaData = AnimCurveMetaData->GetCurveMetaData(NameInfo.OldName))
			{
				if (CurveMetaData->Type.bMorphtarget)
				{
					AnimCurveMetaData->RenameCurveMetaData(NameInfo.OldName, NameInfo.NewName);
				}
			}	
		}
	
		for (const FName OldCurve : GetMorphTargetChangeTracker().GetCurvesToRemove())
		{
			// Only remove if we have a morph flag set
			if (FCurveMetaData* CurveMetaData = AnimCurveMetaData->GetCurveMetaData(OldCurve))
			{
				if (CurveMetaData->Type.bMorphtarget)
				{
					AnimCurveMetaData->RemoveCurveMetaData(OldCurve);
				}
			}	
		}
	
		for (const FName NewCurve : GetMorphTargetChangeTracker().GetCurvesToAdd())
		{
			AnimCurveMetaData->AddCurveMetaData(NewCurve);
		
			// Ensure we have a morph flag set
			if (FCurveMetaData* CurveMetaData = AnimCurveMetaData->GetCurveMetaData(NewCurve))
			{
				AnimCurveMetaData->Modify();
				CurveMetaData->Type.bMorphtarget = true;
			}	
		}
	}

	FTrackerChangeScope(this);
	ResetTrackersDirect();
	

}

bool USkeletalMeshBackedDynamicMeshComponent::IsCommitting() const
{
	return bIsCommiting;
}

void USkeletalMeshBackedDynamicMeshComponent::DiscardChanges()
{
	// Records a change that turns current mesh back to the initial state
	FChangeScope ChangeScope(this);
	
	FDynamicMesh3 InitialMeshCopy = *Trackers.InitialAssetMesh;
	SetMesh(MoveTemp(InitialMeshCopy));

	ResetTrackersDirect();
}


void USkeletalMeshBackedDynamicMeshComponent::HandleSkeletonChange(USkeletonModifier* InModifier)
{
	FTrackerChangeScope ChangeScope(this);
	
	Trackers.RefSkeleton = InModifier->GetReferenceSkeleton();
	Trackers.RefSkeleton.GetBoneAbsoluteTransforms(Trackers.ComponentSpaceBoneTransformsRefPose);
	Trackers.SkeletonChangeTracker.HandleSkeletonChanged(InModifier->GetBoneIndexTracker());
	MarkDirtyInternal();
}

void USkeletalMeshBackedDynamicMeshComponent::ForwardVisibilityChangeRequest(bool bInVisible)
{
	// This component is typically hidden as it only serves as data container
	// A separate mesh should be used to preview the skeletal mesh represented by this component,
	// which should receive visibility updates instead.
	OnRequestingVisibilityChangeDelegate.Broadcast(bInVisible);
}

FName USkeletalMeshBackedDynamicMeshComponent::AddMorphTarget(FName InName)
{
	FName ActualName = GetAvailableMorphTargetName(InName);
	
	FChangeScope ChangeScope(this);

	// Mesh change
	EditMesh([&](FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;
		FDynamicMeshMorphTargetAttribute* MorphTargetAttribute = new FDynamicMeshMorphTargetAttribute(&Mesh);
		MorphTargetAttribute->SetName(ActualName);
		Mesh.Attributes()->AttachMorphTargetAttribute(ActualName, MorphTargetAttribute);
	});

	// Tracker change
	Trackers.MorphTargetChangeTracker.HandleAddMorphTarget(ActualName);
	MarkDirtyInternal();

	return ActualName;
}

FName USkeletalMeshBackedDynamicMeshComponent::RenameMorphTarget(FName InOldName, FName InNewName)
{
	if (InOldName == InNewName)
	{
		return InNewName;
	}
	
	if (!GetMorphTargetChangeTracker().GetCurrentMorphTargetNames().Contains(InOldName))
	{
		return NAME_None;
	}

	
	FName ActualNewName = GetAvailableMorphTargetName(InNewName, InOldName);

	FChangeScope ChangeScope(this);

	EditMesh([&](FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;
		FDynamicMeshMorphTargetAttribute* OldMorphTargetAttribute = Mesh.Attributes()->GetMorphTargetAttribute(InOldName);
			
		FDynamicMeshMorphTargetAttribute* NewMorphTargetAttribute = new FDynamicMeshMorphTargetAttribute(&Mesh);
		NewMorphTargetAttribute->SetName(ActualNewName);
		*NewMorphTargetAttribute = MoveTemp(*OldMorphTargetAttribute);
			
		Mesh.Attributes()->AttachMorphTargetAttribute(ActualNewName, NewMorphTargetAttribute);
		Mesh.Attributes()->RemoveMorphTargetAttribute(InOldName);
	});

	Trackers.MorphTargetChangeTracker.HandleRenameMorphTarget(InOldName, ActualNewName);
	MarkDirtyInternal();

	return ActualNewName;
}

void USkeletalMeshBackedDynamicMeshComponent::RemoveMorphTargets(const TArray<FName>& InNames)
{
	TArray<FName> MorphsToRemove;
	for (const FName& Name : InNames)
	{
		if (GetMorphTargetChangeTracker().GetCurrentMorphTargetNames().Contains(Name))
		{
			MorphsToRemove.Add(Name);
		}
	}
	
	if (MorphsToRemove.IsEmpty())
	{
		return;
	}

	FChangeScope ChangeScope(this);
	
	EditMesh([&](FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;
		for (const FName& Name : MorphsToRemove)
		{
			Mesh.Attributes()->RemoveMorphTargetAttribute(Name);
		}
	});

	for (const FName& Name : MorphsToRemove)
	{
		Trackers.MorphTargetChangeTracker.HandleRemoveMorphTarget(Name);
	}

	MarkDirtyInternal();
}

TArray<FName> USkeletalMeshBackedDynamicMeshComponent::DuplicateMorphTargets(const TArray<FName>& InNames)
{
	TArray<FName> MorphsToDuplicate;
	for (const FName& Name : InNames)
	{
		if (GetMorphTargetChangeTracker().GetCurrentMorphTargetNames().Contains(Name))
		{
			MorphsToDuplicate.Add(Name);
		}
	}
	
	if (MorphsToDuplicate.IsEmpty())
	{
		return {};
	}

	FChangeScope ChangeScope(this);

	TArray<FName> NewMorphs;
	EditMesh([&](FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;
		for (const FName& Name : MorphsToDuplicate)
		{
			FName DuplicateName = GetAvailableMorphTargetName(Name);
			NewMorphs.Add(DuplicateName);
			
			using namespace UE::Geometry;
			FDynamicMeshMorphTargetAttribute* SourceMorphTargetAttribute = Mesh.Attributes()->GetMorphTargetAttribute(Name);
			
			FDynamicMeshMorphTargetAttribute* DuplicatedMorphTargetAttribute = new FDynamicMeshMorphTargetAttribute(&Mesh);
			DuplicatedMorphTargetAttribute->SetName(DuplicateName);
			*DuplicatedMorphTargetAttribute = (*SourceMorphTargetAttribute);
			
			Mesh.Attributes()->AttachMorphTargetAttribute(DuplicateName, DuplicatedMorphTargetAttribute);
		}
	});
	
	// Tracker change
	for (const FName& Name : NewMorphs)
	{
		Trackers.MorphTargetChangeTracker.HandleAddMorphTarget(Name);
		Trackers.MorphTargetChangeTracker.HandleEditMorphTarget(Name);
	}

	
	return NewMorphs;
}

void USkeletalMeshBackedDynamicMeshComponent::MarkMorphTargetEdited(FName InName)
{
	FTrackerChangeScope Scope(this);
	
	// Mesh should be marked dirty during tool commit already 
	Trackers.MorphTargetChangeTracker.HandleEditMorphTarget(InName);

}

bool USkeletalMeshBackedDynamicMeshComponent::IsSkeletonDirty() const
{
	return GetSkeletonChangeTracker().GetChangeCount() != 0;
}

bool USkeletalMeshBackedDynamicMeshComponent::IsDirty() const
{
	return GetChangeCount() != 0;
}

int32 USkeletalMeshBackedDynamicMeshComponent::GetChangeCount() const
{
	return Trackers.ChangeCount;
}

USkeletalMesh* USkeletalMeshBackedDynamicMeshComponent::GetSkeletalMesh() const
{
	return WeakSkeletalMesh.Get();
}

void USkeletalMeshBackedDynamicMeshComponent::ResetTrackersDirect()
{
	Trackers.InitialAssetMesh = GetMeshCopy();

	Trackers.ChangeCount = 0;
	
	Trackers.RefSkeleton = WeakSkeletalMesh->GetRefSkeleton();
	Trackers.RefSkeleton.GetBoneAbsoluteTransforms(Trackers.ComponentSpaceBoneTransformsRefPose);
	Trackers.SkeletonChangeTracker.Init(Trackers.RefSkeleton.GetRawBoneNum());
	
	TArray<FName> MorphTargetNames;
	GetMesh()->Attributes()->GetMorphTargetAttributes().GenerateKeyArray(MorphTargetNames);
	Trackers.MorphTargetChangeTracker.Init(MorphTargetNames);
}

FName USkeletalMeshBackedDynamicMeshComponent::GetAvailableMorphTargetName(FName InNewName, FName InOldName) const
{
	TArray<FName> MorphTargets = GetMorphTargetChangeTracker().GetCurrentMorphTargetNames();

	MorphTargets.Remove(InOldName);
	
	FName ActualName = InNewName;	
	while (MorphTargets.Contains(ActualName))
	{
		ActualName.SetNumber(ActualName.GetNumber() + 1);
	}

	return ActualName;
}

EMeshLODIdentifier USkeletalMeshBackedDynamicMeshComponent::GetValidEditingLOD(EMeshLODIdentifier InLOD) const
{
	constexpr bool bSkipAutoGenerated= true;
	TArray<EMeshLODIdentifier> AssetAvailableLODs = UE::ToolTarget::GetAvailableLODs(WeakSkeletalMesh.Get(), bSkipAutoGenerated);

	if (!AssetAvailableLODs.Contains(InLOD))
	{
		return AssetAvailableLODs[0];
	}

	return InLOD;
}

TSharedPtr<FDynamicMesh3> USkeletalMeshBackedDynamicMeshComponent::GetMeshCopy()
{
	TSharedPtr<FDynamicMesh3> Mesh = MakeShared<FDynamicMesh3>();
	ProcessMesh([&](const FDynamicMesh3& ReadMesh) { *Mesh = ReadMesh; });
	return Mesh;	
}

void USkeletalMeshBackedDynamicMeshComponent::MarkDirtyInternal()
{
	Trackers.ChangeCount++;
}


void USkeletalMeshBackedDynamicMeshComponent::MarkDirty()
{
	FTrackerChangeScope ChangeScope(this);

	MarkDirtyInternal();
}



USkeletalMeshBackedDynamicMeshComponent::FTrackerChange::FTrackerChange(USkeletalMeshBackedDynamicMeshComponent* InComponent)
{
	OldTrackers = InComponent->Trackers;
	NewTrackers = OldTrackers; 
}

FString USkeletalMeshBackedDynamicMeshComponent::FTrackerChange::ToString() const
{
	return TEXT("State Changed");
}

void USkeletalMeshBackedDynamicMeshComponent::FTrackerChange::Apply(UObject* Object)
{
	if (USkeletalMeshBackedDynamicMeshComponent* Component = Cast<USkeletalMeshBackedDynamicMeshComponent>(Object))
	{
		Component->Trackers = NewTrackers;
	}	
}

void USkeletalMeshBackedDynamicMeshComponent::FTrackerChange::Revert(UObject* Object)
{
	if (USkeletalMeshBackedDynamicMeshComponent* Component = Cast<USkeletalMeshBackedDynamicMeshComponent>(Object))
	{
		Component->Trackers = OldTrackers;
	}	
}

void USkeletalMeshBackedDynamicMeshComponent::FTrackerChange::Close(USkeletalMeshBackedDynamicMeshComponent* InComponent)
{
	NewTrackers = InComponent->Trackers;	
}

USkeletalMeshBackedDynamicMeshComponent::FChangeScope::FChangeScope(USkeletalMeshBackedDynamicMeshComponent* InComponent, bool bInRecordMeshChange)
{
	Component = InComponent;
	bRecordMeshChange = bInRecordMeshChange;
	
	if (GUndo)
	{
		TrackerChange = MakeUnique<FTrackerChange>(Component);

		if (bRecordMeshChange)
		{
			OldMesh = Component->GetMeshCopy();
			NewMesh = OldMesh;
		}
	}
	
}

USkeletalMeshBackedDynamicMeshComponent::FChangeScope::~FChangeScope()
{
	if (GUndo)
	{
		TrackerChange->Close(Component);

		GUndo->StoreUndo(Component, MoveTemp(TrackerChange));	
		
		if (bRecordMeshChange)
		{
			NewMesh = Component->GetMeshCopy();

			TUniquePtr<FMeshReplacementChange> ReplaceChange = MakeUnique<FMeshReplacementChange>(OldMesh, NewMesh);

			Component->GetDynamicMesh()->Modify();
			GUndo->StoreUndo(Component->GetDynamicMesh(), MoveTemp(ReplaceChange));	
		}
	}
}

#undef LOCTEXT_NAMESPACE
