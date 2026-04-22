// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterUtils.h"

#include "Filters/CurveEditorFilterBase.h"
#include "Modification/Utils/ScopedSelectionChange.h"

#include "Containers/Map.h"
#include "Modification/Utils/ScopedCurveChange.h"
#include "Templates/SharedPointer.h"

namespace UE::CurveEditor::FilterUtils
{
	void ApplyFilter(const TSharedRef<FCurveEditor>& InCurveEditor, UCurveEditorFilterBase& InFilter)
	{
		ApplyFilter(InCurveEditor, InFilter, InCurveEditor->Selection.GetAll());
	}

	void ApplyFilter(
		const TSharedRef<FCurveEditor>& InCurveEditor,
		UCurveEditorFilterBase& InFilter,
		const TMap<FCurveModelID, FKeyHandleSet>& InSelectedKeys
		)
	{
		using namespace UE::CurveEditor;
		
		TMap<FCurveModelID, FKeyHandleSet> OutKeysToSelect;
		{
			const FScopedCurveChange KeyChange(FCurvesSnapshotBuilder(InCurveEditor, InSelectedKeys));
			InFilter.ApplyFilter(InCurveEditor, InSelectedKeys, OutKeysToSelect);
		}
		
		const FScopedSelectionChange Transaction(InCurveEditor);
		// Clear their selection and then set it to the keys the filter thinks you should have selected.
		InCurveEditor->GetSelection().Clear();

		for (const TTuple<FCurveModelID, FKeyHandleSet>& OutSet : OutKeysToSelect)
		{
			InCurveEditor->GetSelection().Add(OutSet.Key, ECurvePointType::Key, OutSet.Value.AsArray());
		}
	}
}