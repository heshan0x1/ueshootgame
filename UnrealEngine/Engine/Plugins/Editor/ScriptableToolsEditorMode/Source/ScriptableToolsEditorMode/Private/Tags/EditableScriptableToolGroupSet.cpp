// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tags/EditableScriptableToolGroupSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditableScriptableToolGroupSet)

UEditableScriptableToolGroupSet::UEditableScriptableToolGroupSet()
	: GroupsProperty(nullptr)
{
	if (!IsTemplate())
	{
		GroupsProperty = FindFProperty<FProperty>(GetClass(), TEXT("GroupSet"));
	}
}

void UEditableScriptableToolGroupSet::SetGroups(const FScriptableToolGroupSet::FGroupSet& InGroups)
{
	GroupSet.SetGroups(InGroups);
}

FScriptableToolGroupSet::FGroupSet& UEditableScriptableToolGroupSet::GetGroups()
{
	return GroupSet.GetGroups();
}

FString UEditableScriptableToolGroupSet::GetGroupSetExportText()
{
	GroupsPropertyAsString.Reset();

	if (GroupsProperty)
	{
		GroupsProperty->ExportTextItem_Direct(GroupsPropertyAsString, &GroupSet, &GroupSet, this, 0);
	}

	return GroupsPropertyAsString;
}
