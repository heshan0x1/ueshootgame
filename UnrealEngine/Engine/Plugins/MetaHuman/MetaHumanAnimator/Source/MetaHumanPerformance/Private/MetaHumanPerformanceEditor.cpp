// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPerformanceEditor.h"
#include "MetaHumanPerformanceEditorToolkit.h"
#include "MetaHumanPerformance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanPerformanceEditor)

void UMetaHumanPerformanceEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(ObjectToEdit);
}

TSharedPtr<FBaseAssetToolkit> UMetaHumanPerformanceEditor::CreateToolkit()
{
	return MakeShared<FMetaHumanPerformanceEditorToolkit>(this);
}

void UMetaHumanPerformanceEditor::SetObjectToEdit(UObject* InObject)
{
	ObjectToEdit = InObject;
}
