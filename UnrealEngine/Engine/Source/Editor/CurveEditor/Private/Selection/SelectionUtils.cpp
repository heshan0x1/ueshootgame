// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectionUtils.h"

#include "CurveEditor.h"
#include "CurveEditorTrace.h"
#include "SCurveEditor.h"
#include "Selection/ScopedSelectionChangeEventSuppression.h"

namespace UE::CurveEditor
{
namespace SelectionUtilsPrivate
{
static bool CleanseSingle(
	const TSharedRef<FCurveEditor>& InCurveEditor, FCurveEditorSelection& InSelection,
	const FCurveModelID& InModelId, const FKeyHandleSet& InSet
	)
{
	SCOPED_CURVE_EDITOR_TRACE(CleanseSingle);
	
	FCurveModel* CurveModel = InCurveEditor->FindCurve(InModelId);

	// If the entire curve was removed, just dump that out of the selection set.
	if (!CurveModel)
	{
		InSelection.Remove(InModelId);
		return true;
	}
	// Get all of the key handles from this curve.
	const TArray<FKeyHandle> KeyHandles = CurveModel->GetAllKeys();

	// The set handles will be mutated as we remove things so we need a copy that we can iterate through.
	const TConstArrayView<FKeyHandle> SelectedHandles = InSet.AsArray();
	const TArray<FKeyHandle> NonMutableArray(SelectedHandles.GetData(), SelectedHandles.Num());

	bool bContainedStaleKeys = false;
	for (const FKeyHandle& Handle : NonMutableArray)
	{
		// Check to see if our curve model contains this handle still.
		if (!KeyHandles.Contains(Handle))
		{
			InSelection.Remove(InModelId, ECurvePointType::Key, Handle);
			bContainedStaleKeys = true;
		}
	}
	return bContainedStaleKeys;
}
}
	
ECleanseResult CleanseSelection(
	const TSharedRef<FCurveEditor>& InCurveEditor, FCurveEditorSelection& InSelection
	)
{
	SCOPED_CURVE_EDITOR_TRACE(CleanseSelection);
	// Minor optimization to reduce selection changed calls
	const FScopedSelectionChangeEventSuppression SupressSelectionEvents(InCurveEditor);

	bool bHadStaleCurvesOrKeys = false;
	TMap<FCurveModelID, FKeyHandleSet> SelectionSet = InSelection.GetAll();
	for (const TPair<FCurveModelID, FKeyHandleSet>& Set : SelectionSet)
	{
		bHadStaleCurvesOrKeys |= SelectionUtilsPrivate::CleanseSingle(InCurveEditor, InSelection, Set.Key, Set.Value);
	}

	return bHadStaleCurvesOrKeys ? ECleanseResult::HadStaleKeys : ECleanseResult::NoStaleKeys;
}

ECleanseResult CleanseSelection(
	const TSharedRef<FCurveEditor>& InCurveEditor, FCurveEditorSelection& InSelection, TConstArrayView<FCurveModelID> InOnlyTheseCurves
	)
{
	SCOPED_CURVE_EDITOR_TRACE(CleanseSelection);
	// Minor optimization to reduce selection changed calls
	const FScopedSelectionChangeEventSuppression SupressSelectionEvents(InCurveEditor);
	
	bool bHadStaleCurvesOrKeys = false;
	for (const FCurveModelID& CurveId : InOnlyTheseCurves)
	{
		if (const FKeyHandleSet* Set = InSelection.FindForCurve(CurveId))
		{
			bHadStaleCurvesOrKeys |= SelectionUtilsPrivate::CleanseSingle(InCurveEditor, InSelection, CurveId, *Set);
		}
	}
	
	return bHadStaleCurvesOrKeys ? ECleanseResult::HadStaleKeys : ECleanseResult::NoStaleKeys;
}
}
