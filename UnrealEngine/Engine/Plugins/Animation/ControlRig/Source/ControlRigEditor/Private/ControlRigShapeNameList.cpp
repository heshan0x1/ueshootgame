// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigShapeNameList.h"
#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/SRigVMGraphPinNameListValueWidget.h"
#include "ControlRig.h"
#include "ControlRigAsset.h"

#define LOCTEXT_NAMESPACE "ControlRigShapeNameList"

namespace UE::ControlRigEditor
{

void FControlRigShapeNameList::CreateShapeLibraryListWidget(IDetailChildrenBuilder& InStructBuilder, TSharedPtr<IPropertyHandle>& InShapeSettingsNameProperty)
{
	if (!InShapeSettingsNameProperty.IsValid())
	{
		return;
	}

	ShapeSettingsNameProperty = InShapeSettingsNameProperty;

	TSharedPtr<FRigVMStringWithTag> InitialSelected;
	const FString CurrentShapeName = GetShapeNameListText().ToString();
	for (TSharedPtr<FRigVMStringWithTag> Item : ShapeNameList)
	{
		if (Item->Equals(CurrentShapeName))
		{
			InitialSelected = Item;
		}
	}

	IDetailPropertyRow& Row = InStructBuilder.AddProperty(ShapeSettingsNameProperty.ToSharedRef());

	constexpr bool bShowChildren = true;
	Row.CustomWidget(bShowChildren)
		.NameContent()
		[
			ShapeSettingsNameProperty->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SAssignNew(ShapeNameListWidget, SRigVMGraphPinNameListValueWidget)
				.OptionsSource(&ShapeNameList)
				.OnGenerateWidget(this, &FControlRigShapeNameList::MakeShapeNameListItemWidget)
				.OnSelectionChanged(this, &FControlRigShapeNameList::OnShapeNameListChanged)
				.OnComboBoxOpening(this, &FControlRigShapeNameList::OnShapeNameListComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
						.Text(this, &FControlRigShapeNameList::GetShapeNameListText)
						.Font(IDetailLayoutBuilder::GetDetailFont())
				]
		];
}

void FControlRigShapeNameList::GenerateShapeLibraryList(const UControlRig* ControlRig)
{
	ShapeNameList.Reset();

	if (ControlRig == nullptr)
	{
		return;
	}

	const TMap<FString, FString>& LibraryNameMap = ControlRig->GetShapeLibraryNameMap();
	TArray<TSoftObjectPtr<UControlRigShapeLibrary>> ShapeLibraries = ControlRig->GetShapeLibraries();
	for (const TSoftObjectPtr<UControlRigShapeLibrary>& ShapeLibrary : ShapeLibraries)
	{
		if (ShapeLibrary.IsNull() || !ShapeLibrary.IsValid())
		{
			(void)ShapeLibrary.LoadSynchronous();
		}
		if (ShapeLibrary.IsNull() || !ShapeLibrary.IsValid())
		{
			continue;
		}
		const bool bUseNameSpace = ShapeLibraries.Num() > 1;
		FString LibraryName = ShapeLibrary->GetName();
		if (const FString* RemappedName = LibraryNameMap.Find(LibraryName))
		{
			LibraryName = *RemappedName;
		}

		const FString NameSpace = bUseNameSpace ? LibraryName + TEXT(".") : FString();
		ShapeNameList.Add(MakeShared<FRigVMStringWithTag>(UControlRigShapeLibrary::GetShapeName(ShapeLibrary.Get(), bUseNameSpace, LibraryNameMap, ShapeLibrary->DefaultShape)));
		for (const FControlRigShapeDefinition& Shape : ShapeLibrary->Shapes)
		{
			ShapeNameList.Add(MakeShared<FRigVMStringWithTag>(UControlRigShapeLibrary::GetShapeName(ShapeLibrary.Get(), bUseNameSpace, LibraryNameMap, Shape)));
		}
	}
}

TSharedRef<SWidget> FControlRigShapeNameList::MakeShapeNameListItemWidget(TSharedPtr<FRigVMStringWithTag> InItem)
{
	const FText ItemText = InItem.IsValid() ? FText::FromString(InItem->GetStringWithTag()) : FText();

	return SNew(STextBlock)
		.Text(ItemText)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}
	
void FControlRigShapeNameList::OnShapeNameListChanged(TSharedPtr<FRigVMStringWithTag> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	const FString& NewShapeNameString = NewSelection.IsValid() ? NewSelection->GetString() : FString();
	if(ShapeSettingsNameProperty)
	{
		const FName ShapeName = NewShapeNameString.IsEmpty() ? FName(NAME_None) : FName(*NewShapeNameString);
		ShapeSettingsNameProperty->SetValue(ShapeName);
	}
}
	
void FControlRigShapeNameList::OnShapeNameListComboBox()
{
	const FString ShapeNameListText = GetShapeNameListText().ToString();
	const TSharedPtr<FRigVMStringWithTag>* CurrentlySelectedItem =
		ShapeNameList.FindByPredicate([ShapeNameListText](const TSharedPtr<FRigVMStringWithTag>& InItem)
		{
			return ShapeNameListText == InItem->GetString();
		});
		
	if(CurrentlySelectedItem)
	{
		ShapeNameListWidget->SetSelectedItem(*CurrentlySelectedItem);
	}
}
	
FText FControlRigShapeNameList::GetShapeNameListText() const
{
	if(!ShapeSettingsNameProperty)
	{
		return FText();
	}
		
	TOptional<FString> SharedValue;
	for(int32 Index = 0; Index < ShapeSettingsNameProperty->GetNumPerObjectValues(); Index++)
	{
		FString SingleValue;
		if(!ShapeSettingsNameProperty->GetPerObjectValue(Index, SingleValue))
		{
			SharedValue.Reset();
			break;
		}
		if(!SharedValue.IsSet())
		{
			SharedValue = SingleValue;
		}
		else if(SharedValue.GetValue() != SingleValue)
		{
			SharedValue.Reset();
			break;
		}
	}
		
	if(SharedValue.IsSet())
	{
		return FText::FromString(SharedValue.GetValue());
	}
	return LOCTEXT("MultipleValues", "Multiple Values");
}

} // end namespace UE::ControlRigEditor

#undef LOCTEXT_NAMESPACE
