// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "CurveEditorTypes.h"
#include "CurveKeyDataMap.h"

namespace UE::CurveEditor
{
struct FRemoveKeysChangeData
{
	/**
	 * On undo, add back FKeyDataSnapshot::Keys together with the saved positions and attributes.
	 * On redo, remove the FKeyDataSnapshot::Keys.
	 */
	TMap<FCurveModelID, FCurveKeyData> SavedCurveState;
	
	bool HasChanges() const { return !SavedCurveState.IsEmpty(); }
	operator bool() const { return HasChanges(); }
	SIZE_T GetAllocatedSize() const { return FCurveKeyData::GetAllocatedSize(SavedCurveState); }

	/** Shrinks the containers' used memory to smallest possible to store elements currently in it. */
	void Shrink()
	{
		SavedCurveState.Shrink();
		for (TPair<FCurveModelID, FCurveKeyData>& Pair : SavedCurveState)
		{
			Pair.Value.Shrink();
		}
	}
};
}
