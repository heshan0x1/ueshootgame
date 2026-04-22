// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SSearchableComboBox.h"
#include "QueryEditor/TedsQueryEditorModel.h"

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	class SHierarchyComboWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS( SHierarchyComboWidget ){}
		SLATE_END_ARGS()
		
		void Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel);

	protected:
		void GenerateHierarchyList();
		
	protected:
		TArray<TSharedPtr<FString>> Hierarchies;
		FTedsQueryEditorModel* Model = nullptr;
		TSharedPtr<SSearchableComboBox> SearchableComboBox;
	};
}
