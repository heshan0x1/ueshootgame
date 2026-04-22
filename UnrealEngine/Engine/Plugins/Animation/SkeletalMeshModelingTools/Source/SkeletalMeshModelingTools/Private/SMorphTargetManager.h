// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SkeletalMeshNotifier.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableViewBase.h"
#include "EditorUndoClient.h"

namespace MorphTargetManagerLocal
{
	struct FMorphTargetInfo;
}
typedef TSharedPtr<MorphTargetManagerLocal::FMorphTargetInfo> FMorphTargetInfoPtr;
typedef SListView< FMorphTargetInfoPtr > SMorphTargetManagerListType;


class SSearchBox;
class UMorphTargetModifier;
class SMorphTargetManager;
class FUICommandList;



class SMorphTargetManager : public SCompoundWidget, public FEditorUndoClient
{
public:
	DECLARE_DELEGATE_RetVal(TArray<FName>, FOnGetMorphTargets);
	DECLARE_DELEGATE_RetVal_OneParam(float, FOnGetMorphTargetWeight, FName);
	DECLARE_DELEGATE_TwoParams(FOnSetMorphTargetWeight, FName, float); 
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnGetMorphTargetAutoFill, FName);
	DECLARE_DELEGATE_ThreeParams(FOnSetMorphTargetAutoFill, FName, bool, float);
	DECLARE_DELEGATE_RetVal(FName, FOnGetEditingMorphTarget);
	DECLARE_DELEGATE_OneParam(FOnSetEditingMorphTarget, FName);
	DECLARE_DELEGATE_RetVal_OneParam(FName, FOnAddMorphTarget, FName);
	DECLARE_DELEGATE_RetVal_TwoParams(FName, FOnRenameMorphTarget, FName, FName);
	DECLARE_DELEGATE_OneParam(FOnRemoveMorphTargets, const TArray<FName>&);
	DECLARE_DELEGATE_RetVal_OneParam(TArray<FName>, FOnDuplicateMorphTargets, const TArray<FName>&);

	struct FMorphTargetManagerDelegates
	{
		FOnGetMorphTargets OnGetMorphTargets;

		FOnGetMorphTargetWeight OnGetMorphTargetWeight;
		FOnSetMorphTargetWeight OnSetMorphTargetWeight;
		
		FOnGetMorphTargetAutoFill OnGetMorphTargetAutoFill;
		FOnSetMorphTargetAutoFill OnSetMorphTargetAutoFill;

		FOnGetEditingMorphTarget OnGetEditingMorphTarget;
		FOnSetEditingMorphTarget OnSetEditingMorphTarget;
		
		FOnAddMorphTarget OnAddNewMorphTarget;
		FOnRenameMorphTarget OnRenameMorphTarget;
		FOnRemoveMorphTargets OnRemoveMorphTargets;
		FOnDuplicateMorphTargets OnDuplicateMorphTargets;
	};	
	
	SLATE_BEGIN_ARGS( SMorphTargetManager ) {}
		SLATE_ARGUMENT(FMorphTargetManagerDelegates, Delegates)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	
	/**
	* Destructor - resets the morph targets
	*
	*/
	virtual ~SMorphTargetManager();

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	
	void BindCommands();
	void RefreshList();
	void SelectMorphTargets(const TArray<FName>& MorphTargets);
	
	TSharedRef<SWidget> CreateNewMenuWidget();
	void OnFilterTextChanged(const FText& Text);
	FText GetFilterText() const;
	FText GetHighlightText(FText InName) const;
	
	TSharedRef<class ITableRow> GenerateMorphTargetRow(FMorphTargetInfoPtr MorphTargetItem, const TSharedRef<STableViewBase>& TableViewBase);
	TSharedPtr<SWidget> OnGetContextMenuContent();

	void SetMorphTargetWeight(FName MorphTarget, float Weight);
	float GetMorphTargetWeight(FName MorphTarget);
	void SetMorphTargetAutoFill(FName MorphTarget, bool bAutoFill, float PreviousOverrideWeight);
	bool GetMorphTargetAutoFill(FName MorphTarget);
	void SetEditingMorphTarget(FName MorphTarget);
	bool IsEditingMorphTarget(FName MorphTarget);

	void AddMorphTarget();
	FName RenameMorphTarget(FName InNewName, FName InOldName);
protected:
	void RenameSelectedMorphTarget();
	bool CanRename();
	void RemoveSelectedMorphTargets();
	bool CanRemove();
	void DuplicateSelectedMorphTargets();
	bool CanDuplicate();
	
	TSharedPtr<SSearchBox> NameFilterBox;
	TSharedPtr<SMorphTargetManagerListType> ListView;

	TArray<FMorphTargetInfoPtr> List;
	TArray<FMorphTargetInfoPtr> FullList;

	TWeakObjectPtr<UMorphTargetModifier> Modifier;

	FMorphTargetManagerDelegates Delegates;

	// Commands
	TSharedPtr<FUICommandList> CommandList;
};

