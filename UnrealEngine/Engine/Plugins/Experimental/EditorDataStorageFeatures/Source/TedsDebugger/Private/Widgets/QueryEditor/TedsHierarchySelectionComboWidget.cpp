// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsHierarchySelectionComboWidget.h"

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

namespace UE::Editor::DataStorage::Debug::QueryEditor
{
	void SHierarchyComboWidget::Construct(const FArguments& InArgs, FTedsQueryEditorModel& InModel)
	{
		Model = &InModel;

		GenerateHierarchyList();
		
		ChildSlot
		[
			SAssignNew(SearchableComboBox, SSearchableComboBox)
            	.OptionsSource(&Hierarchies)
            	.OnSelectionChanged_Lambda([this](TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
            	{
            		if (SelectedItem)
            		{
            			Model->SetHierarchy(FName(*SelectedItem));
            		}
            	})
            	.OnGenerateWidget_Lambda([](TSharedPtr<FString> InItem)
            	{
            		return SNew(STextBlock)
            		.Text(FText::FromString(InItem.IsValid() ? *InItem : FString()));
            	})
				.OnComboBoxOpening_Lambda([this]()
				{
					GenerateHierarchyList();
				})
            	.Content()
            	[
            		SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromName(Model->GetHierarchyName());
					})
            	]
		];
	}

	void SHierarchyComboWidget::GenerateHierarchyList()
	{
		Hierarchies.Empty();
		
		Model->GetTedsInterface().ListHierarchyNames([this](const FName& HierarchyName)
		{
			Hierarchies.Add(MakeShared<FString>(HierarchyName.ToString()));
		});
	}
}
