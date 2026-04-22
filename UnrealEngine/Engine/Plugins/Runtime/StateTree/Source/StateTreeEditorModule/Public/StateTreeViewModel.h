// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "EditorUndoClient.h"
#include "IStateTreeEditorHost.h"
#include "StateTreeViewModel.generated.h"

#define UE_API STATETREEEDITORMODULE_API

namespace UE::StateTreeEditor
{
	struct FClipboardEditorData;
}

struct FStateTreeTransition;
class FMenuBuilder;
class UStateTree;
class UStateTreeEditorData;
class UStateTreeState;

enum class ECheckBoxState : uint8;
enum class EStateTreeBreakpointType : uint8;

struct FPropertyChangedEvent;
struct FStateTreeDebugger;
struct FStateTreeDebuggerBreakpoint;
struct FStateTreeEditorBreakpoint;
struct FStateTreePropertyPathBinding;

enum class EStateTreeViewModelInsert : uint8
{
	Before,
	After,
	Into,
};

enum class UE_DEPRECATED(5.6, "Use the enum with the E prefix") FStateTreeViewModelInsert : uint8
{
	Before,
	After,
	Into,
};

/**
 * ModelView for editing StateTreeEditorData.
 */
class FStateTreeViewModel : public FEditorUndoClient, public TSharedFromThis<FStateTreeViewModel>
{
public:

	DECLARE_MULTICAST_DELEGATE(FOnAssetChanged);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStatesChanged, const TSet<UStateTreeState*>& /*AffectedStates*/, const FPropertyChangedEvent& /*PropertyChangedEvent*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStateAdded, UStateTreeState* /*ParentState*/, UStateTreeState* /*NewState*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStatesRemoved, const TSet<UStateTreeState*>& /*AffectedParents*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStatesMoved, const TSet<UStateTreeState*>& /*AffectedParents*/, const TSet<UStateTreeState*>& /*MovedStates*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStateNodesChanged, const UStateTreeState* /*AffectedState*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, const TArray<TWeakObjectPtr<UStateTreeState>>& /*SelectedStates*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBringNodeToFocus, const UStateTreeState* /*State*/, const FGuid /*NodeID*/);

	UE_API FStateTreeViewModel();
	UE_API virtual ~FStateTreeViewModel() override;

	UE_API void Init(UStateTreeEditorData* InTreeData);

	//~ FEditorUndoClient
	UE_API virtual void PostUndo(bool bSuccess) override;
	UE_API virtual void PostRedo(bool bSuccess) override;

	// Selection handling.
	UE_API void ClearSelection();
	UE_API void SetSelection(UStateTreeState* Selected);
	UE_API void SetSelection(const TArray<TWeakObjectPtr<UStateTreeState>>& InSelection);
	UE_API bool IsSelected(const UStateTreeState* State) const;
	UE_API bool IsChildOfSelection(const UStateTreeState* State) const;
	UE_API void GetSelectedStates(TArray<UStateTreeState*>& OutSelectedStates) const;
	UE_API void GetSelectedStates(TArray<TWeakObjectPtr<UStateTreeState>>& OutSelectedStates) const;
	UE_API bool HasSelection() const;

	UE_API void BringNodeToFocus(UStateTreeState* State, const FGuid NodeID);
	
	// Returns associated state tree asset.
	UE_API const UStateTree* GetStateTree() const;

	UE_API const UStateTreeEditorData* GetStateTreeEditorData() const;

	UE_API const UStateTreeState* GetStateByID(const FGuid StateID) const;
	UE_API UStateTreeState* GetMutableStateByID(const FGuid StateID) const;
	
	// Returns array of subtrees to edit.
	UE_API TArray<TObjectPtr<UStateTreeState>>* GetSubTrees() const;
	UE_API int32 GetSubTreeCount() const;
	UE_API void GetSubTrees(TArray<TWeakObjectPtr<UStateTreeState>>& OutSubtrees) const;

	/** Find the states that are linked to the provided StateID. */
	UE_API void GetLinkStates(FGuid StateID, TArray<FGuid>& LinkingIn, TArray<FGuid>& LinkedOut) const;

	// Gets and sets StateTree view expansion state store in the asset.
	UE_API void SetPersistentExpandedStates(TSet<TWeakObjectPtr<UStateTreeState>>& InExpandedStates);
	UE_API void GetPersistentExpandedStates(TSet<TWeakObjectPtr<UStateTreeState>>& OutExpandedStates);

	// State manipulation
	UE_API void AddState(UStateTreeState* AfterState);
	UE_API void AddChildState(UStateTreeState* ParentState);
	UE_API void RenameState(UStateTreeState* State, FName NewName);
	UE_API void RemoveSelectedStates();
	UE_API void CopySelectedStates();
	UE_API bool CanPasteStatesFromClipboard() const;
	UE_API void PasteStatesFromClipboard(UStateTreeState* AfterState);
	UE_API void PasteStatesAsChildrenFromClipboard(UStateTreeState* ParentState);
	UE_API void DuplicateSelectedStates();
	UE_API void MoveSelectedStatesBefore(UStateTreeState* TargetState);
	UE_API void MoveSelectedStatesAfter(UStateTreeState* TargetState);
	UE_API void MoveSelectedStatesInto(UStateTreeState* TargetState);
	UE_API bool CanEnableStates() const;
	UE_API bool CanDisableStates() const;
	UE_API bool CanPasteNodesToSelectedStates() const;
	UE_API void SetSelectedStatesEnabled(bool bEnable);

	// EditorNode and Transition manipulation
	// @todo: support ReplaceWith and Rename
	UE_API void DeleteNode(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);
	UE_API void DeleteAllNodes(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);
	UE_API void CopyNode(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);
	UE_API void CopyAllNodes(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);
	UE_API void PasteNode(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);
	UE_API void PasteNodesToSelectedStates();
	UE_API void DuplicateNode(TWeakObjectPtr<UStateTreeState> State, const FGuid& ID);

	// Force to update the view externally.
	UE_API void NotifyAssetChangedExternally() const;
	UE_API void NotifyStatesChangedExternally(const TSet<UStateTreeState*>& ChangedStates, const FPropertyChangedEvent& PropertyChangedEvent) const;

	// Debugging
#if WITH_STATETREE_TRACE_DEBUGGER
	UE_API bool HasBreakpoint(FGuid ID, EStateTreeBreakpointType Type);
	UE_API bool CanProcessBreakpoints() const;
	UE_API bool CanAddStateBreakpoint(EStateTreeBreakpointType Type) const;
	UE_API bool CanRemoveStateBreakpoint(EStateTreeBreakpointType Type) const;
	UE_API ECheckBoxState GetStateBreakpointCheckState(EStateTreeBreakpointType Type) const;
	UE_API void HandleEnableStateBreakpoint(EStateTreeBreakpointType Type);
	UE_API void ToggleStateBreakpoints(TConstArrayView<TWeakObjectPtr<>> States, EStateTreeBreakpointType Type);
	UE_API void ToggleTaskBreakpoint(FGuid ID, EStateTreeBreakpointType Type);
	UE_API void ToggleTransitionBreakpoint(TConstArrayView<TNotNull<const FStateTreeTransition*>> Transitions, ECheckBoxState ToggledState);

	UE_API UStateTreeState* FindStateAssociatedToBreakpoint(FStateTreeDebuggerBreakpoint Breakpoint) const;

	TSharedRef<FStateTreeDebugger> GetDebugger() const
	{
		return Debugger;
	}

	UE_API void RemoveAllBreakpoints();
	UE_API void RefreshDebuggerBreakpoints();
#endif // WITH_STATETREE_TRACE_DEBUGGER

	UE_API bool IsStateActiveInDebugger(const UStateTreeState& State) const;

	// Called when the whole asset is updated (i.e. undo/redo).
	FOnAssetChanged& GetOnAssetChanged()
	{
		return OnAssetChanged;
	}
	
	// Called when States are changed (i.e. change name or properties).
	FOnStatesChanged& GetOnStatesChanged()
	{
		return OnStatesChanged;
	}
	
	// Called each time a state is added.
	FOnStateAdded& GetOnStateAdded()
	{
		return OnStateAdded;
	}

	// Called each time a states are removed.
	FOnStatesRemoved& GetOnStatesRemoved()
	{
		return OnStatesRemoved;
	}

	// Called each time a state is removed.
	FOnStatesMoved& GetOnStatesMoved()
	{
		return OnStatesMoved;
	}

	// Called each time a state's Editor nodes or transitions are changed except from the DetailsView.
	FOnStateNodesChanged& GetOnStateNodesChanged()
	{
		return OnStateNodesChanged;
	}

	// Called each time the selection changes.
	FOnSelectionChanged& GetOnSelectionChanged()
	{
		return OnSelectionChanged;
	}

	FOnBringNodeToFocus& GetOnBringNodeToFocus()
	{
		return OnBringNodeToFocus;
	}

protected:
	UE_API void GetExpandedStatesRecursive(UStateTreeState* State, TSet<TWeakObjectPtr<UStateTreeState>>& ExpandedStates);

	UE_API void MoveSelectedStates(UStateTreeState* TargetState, const EStateTreeViewModelInsert RelativeLocation);

	UE_API void PasteStatesAsChildrenFromText(const FString& TextToImport, UStateTreeState* ParentState, const int32 IndexToInsertAt);

	UE_API void HandleIdentifierChanged(const UStateTree& StateTree) const;
	
	UE_API void BindToDebuggerDelegates();

	UE_API void PasteNodesToState(TNotNull<UStateTreeEditorData*> InEditorData, TNotNull<UStateTreeState*> InState, UE::StateTreeEditor::FClipboardEditorData& InProcessedClipboard);

	TWeakObjectPtr<UStateTreeEditorData> TreeDataWeak;
	TSet<TWeakObjectPtr<UStateTreeState>> SelectedStates;

#if WITH_STATETREE_TRACE_DEBUGGER
	UE_API void HandleBreakpointsChanged(const UStateTree& StateTree);
	UE_API void HandlePostCompile(const UStateTree& StateTree);

	TSharedRef<FStateTreeDebugger> Debugger;
	TArray<FGuid> ActiveStates;
#endif // WITH_STATETREE_TRACE_DEBUGGER
	
	FOnAssetChanged OnAssetChanged;
	FOnStatesChanged OnStatesChanged;
	FOnStateAdded OnStateAdded;
	FOnStatesRemoved OnStatesRemoved;
	FOnStatesMoved OnStatesMoved;
	FOnStateNodesChanged OnStateNodesChanged;
	FOnSelectionChanged OnSelectionChanged;
	FOnBringNodeToFocus OnBringNodeToFocus;
};

/** Helper class to allow to copy bindings into clipboard. */
UCLASS(MinimalAPI, Hidden)
class UStateTreeClipboardBindings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FStateTreePropertyPathBinding> Bindings;
};

#undef UE_API
