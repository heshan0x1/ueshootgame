// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointerFwd.h"

struct FCurveModelID;
struct FCurveEditorSelection;
class FCurveEditor;

namespace UE::CurveEditor
{
enum class ECleanseResult : uint8 { NoStaleKeys, HadStaleKeys };
	
/** Removes all curve models and keys from InSelection that do not exist in InCurveEditor. */
ECleanseResult CleanseSelection(const TSharedRef<FCurveEditor>& InCurveEditor, FCurveEditorSelection& InSelection);

/** Overload that will only remove 1. those FKeyHandles belonging to curves in InOnlyTheseCurves, 2. the curves in InOnlyTheseCurves. */
ECleanseResult CleanseSelection(const TSharedRef<FCurveEditor>& InCurveEditor, FCurveEditorSelection& InSelection,
	TConstArrayView<FCurveModelID> InOnlyTheseCurves
	);
}
