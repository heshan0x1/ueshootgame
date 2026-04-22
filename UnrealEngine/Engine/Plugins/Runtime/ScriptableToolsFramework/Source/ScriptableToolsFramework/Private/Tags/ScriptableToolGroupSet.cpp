// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tags/ScriptableToolGroupSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptableToolGroupSet)

void FScriptableToolGroupSet::SanitizeGroups()
{
	FGroupSet GroupSetCopy = Groups;
	GroupSetCopy.Remove(nullptr);
	Groups = GroupSetCopy;
}

