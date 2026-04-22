// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/RigVMEdGraph.h"
#include "RigVMHost.h"
#include "RigVMBlueprintLegacy.h"
#include "Widgets/Views/STreeView.h"
#include "RigVMCore/RigVM.h"
#include "RigVMBlueprintLegacy.h"

#define UE_API RIGVMEDITOR_API

class IRigVMEditor;
class SRigVMExecutionStackView;
class FUICommandList;
class SSearchBox;

namespace ERigStackEntry
{
	enum Type
	{
		Operator,
		Info,
		Warning,
		Error,
	};
}

/** An item in the stack */
class FRigStackEntry : public TSharedFromThis<FRigStackEntry>
{
public:
	UE_API FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InInstructionIndex, ERigVMOpCode InOpCode, const FString& InLabel, const FRigVMASTProxy& InProxy);

	UE_DEPRECATED(5.7, "Use TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigVMExecutionStackView> InStackView, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint); instead")
	UE_API TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigVMExecutionStackView> InStackView, TWeakObjectPtr<UObject> InBlueprint);
	UE_API TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList>
	                                               InCommandList, TSharedPtr<SRigVMExecutionStackView> InStackView, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint);

	int32 EntryIndex;
	ERigStackEntry::Type EntryType;
	int32 InstructionIndex;
	FString CallPath;
	FRigVMCallstack Callstack;
	ERigVMOpCode OpCode;
	FString Label;
	TArray<TSharedPtr<FRigStackEntry>> Children;
	mutable TOptional<bool> ShowAsFadedOut;
	mutable TOptional<FText> VisitedCountText;
	mutable TOptional<FText> DurationText;
	mutable double MicroSeconds;
	mutable TArray<double> MicroSecondsFrames;
};

class SRigStackItem : public STableRow<TSharedPtr<FRigStackEntry>>
{
	SLATE_BEGIN_ARGS(SRigStackItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigStackEntry> InStackEntry, TSharedRef<FUICommandList> InCommandList, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint);

private:
	TWeakPtr<FRigStackEntry> WeakStackEntry;
	TWeakInterfacePtr<IRigVMAssetInterface> WeakBlueprint;
	TWeakPtr<FUICommandList> WeakCommandList;

	FText GetIndexText() const;
	FText GetLabelText() const;
	FSlateColor GetLabelColorAndOpacity() const;
	FText GetTooltip() const;
	FText GetVisitedCountText() const;
	FText GetDurationText() const;
};

class SRigVMExecutionStackView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRigVMExecutionStackView) {}
	SLATE_END_ARGS()

	UE_API ~SRigVMExecutionStackView();

	UE_API void Construct(const FArguments& InArgs, TSharedRef<IRigVMEditor> InRigVMEditor);

	/** Set Selection Changed */
	UE_API void OnSelectionChanged(TSharedPtr<FRigStackEntry> Selection, ESelectInfo::Type SelectInfo);

	UE_API TSharedPtr< SWidget > CreateContextMenu();

protected:

	/** Rebuild the tree view */
	UE_API void RefreshTreeView(URigVM* InVM, FRigVMExtendedExecuteContext* InVMContext);

private:

	/** Bind commands that this widget handles */
	UE_API void BindCommands();

	/** Make a row widget for the table */
	UE_API TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FRigStackEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint);

	/** Get children for the tree */
	UE_API void HandleGetChildrenForTree(TSharedPtr<FRigStackEntry> InItem, TArray<TSharedPtr<FRigStackEntry>>& OutChildren);

	/** Focus on the selected operator in the graph*/
	UE_API void HandleFocusOnSelectedGraphNode();

	/** Offers a dialog to move to a specific instruction */
	UE_API void HandleGoToInstruction();

	/** Selects the target instructions for the current selection */
	UE_API void HandleSelectTargetInstructions();

	UE_API TArray<TSharedPtr<FRigStackEntry>> GetTargetItems(const TArray<TSharedPtr<FRigStackEntry>>& InItems) const;

	UE_API void UpdateTargetItemHighlighting();

	UE_API void OnVMCompiled(UObject* InCompiledObject, URigVM* InCompiledVM, FRigVMExtendedExecuteContext& InVMContext);
	UE_API void OnObjectBeingDebuggedChanged(UObject* InObjectBeingDebugged);

	/** Search box widget */
	TSharedPtr<SSearchBox> FilterBox;
	FText FilterText;
	UE_API void OnFilterTextChanged(const FText& SearchText);


	bool bSuspendModelNotifications;
	bool bSuspendControllerSelection;
	UE_API void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);
	UE_API void HandleHostInitializedEvent(URigVMHost* InHost, const FName& InEventName);
	UE_API void HandleHostExecutedEvent(URigVMHost* InHost, const FName& InEventName);
	UE_API void HandlePreviewHostUpdated(IRigVMEditor* InEditor);
	UE_API void HandleItemMouseDoubleClick(TSharedPtr<FRigStackEntry> InItem);

	/** Populate the execution stack with descriptive names for each instruction */
	UE_API void PopulateStackView(URigVM* InVM, FRigVMExtendedExecuteContext* InVMContext);

	void BindHostDelegates();
	void BindHostDelegates(UObject* InObjectBeingDebugged);
	void UnbindHostDelegates();
	void UnbindAssetDelegates();

	TSharedPtr<STreeView<TSharedPtr<FRigStackEntry>>> TreeView;

	/** Command list we bind to */
	TSharedPtr<FUICommandList> CommandList;

	TWeakPtr<IRigVMEditor> WeakEditor;
	TWeakInterfacePtr<IRigVMAssetInterface> WeakRigVMBlueprint;

	TArray<TSharedPtr<FRigStackEntry>> Operators;

	FDelegateHandle OnModelModified;
	FDelegateHandle OnHostInitializedHandle;
	FDelegateHandle OnHostExecutedHandle;
	FDelegateHandle OnVMCompiledHandle;
	FDelegateHandle OnPreviewHostUpdatedHandle;
	FDelegateHandle OnObjectBeingDebuggedChangedHandle;
	TWeakObjectPtr<URigVMHost> PreviouslyDebuggedHost;
};

#undef UE_API
