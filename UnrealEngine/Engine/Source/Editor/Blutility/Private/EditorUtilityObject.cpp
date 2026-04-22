// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityObject.h"

#include "EditorToolDelegates.h"
/////////////////////////////////////////////////////

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditorUtilityObject)

UEditorUtilityObject::UEditorUtilityObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEditorUtilityObject::PostLoad()
{
	Super::PostLoad();

	FEditorToolDelegates::OnEditorToolStarted.Broadcast(GetClass()->GetName());
}
