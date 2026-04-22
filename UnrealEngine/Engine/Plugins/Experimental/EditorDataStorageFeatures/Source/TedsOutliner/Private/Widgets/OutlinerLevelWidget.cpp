// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutlinerLevelWidget.h"

#include "SceneOutlinerHelpers.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Engine/Level.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OutlinerLevelWidget"

namespace UE::Editor::DataStorage
{
	FText FLevelWidgetSorter::GetShortName() const
	{
		return LOCTEXT("LevelWidgetSorter", "Level Sorter");
	}

	int32 FLevelWidgetSorter::Compare(const FLevelColumn& Left, const FLevelColumn& Right) const
	{
		return Left.Level->GetOuter()->GetName().Compare(Right.Level->GetOuter()->GetName());
	}

	FPrefixInfo FLevelWidgetSorter::CalculatePrefix(const FLevelColumn& Column, uint32 ByteIndex) const
	{
		return CreateSortPrefix(ByteIndex, TSortStringView(FSortCaseSensitive{}, Column.Level->GetOuter()->GetName()));
	}
}

void UOutlinerLevelWidgetFactory::RegisterWidgetConstructors(UE::Editor::DataStorage::ICoreProvider& DataStorage,
	UE::Editor::DataStorage::IUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FOutlinerLevelWidgetConstructor>(
		DataStorageUi.FindPurpose(UE::Editor::DataStorage::IUiProvider::FPurposeInfo("SceneOutliner", "Cell", NAME_None).GeneratePurposeID()),
		TColumn<FLevelColumn>());
	DataStorageUi.RegisterWidgetFactory<FOutlinerLevelWidgetConstructor>(
			DataStorageUi.FindPurpose(DataStorageUi.GetGeneralWidgetPurposeID()),
			TColumn<FLevelColumn>());
}

FOutlinerLevelWidgetConstructor::FOutlinerLevelWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

TSharedPtr<SWidget> FOutlinerLevelWidgetConstructor::CreateWidget(
	UE::Editor::DataStorage::ICoreProvider* DataStorage,
	UE::Editor::DataStorage::IUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle TargetRow,
	UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	using namespace UE::Editor::DataStorage;
	if (!DataStorage->IsRowAvailable(TargetRow))
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("MissingRowReferenceColumn", "Unable to retrieve row reference."));
	}

	FAttributeBinder Binder(TargetRow, DataStorage);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8, 0, 0, 0)
		[
			SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Binder.BindData(&FLevelColumn::Level, [](TWeakObjectPtr<ULevel> InLevel)
				{
					if (InLevel.IsValid())
					{
						return FText::FromString(InLevel->GetOuter()->GetName());
					}
					else
					{
						return LOCTEXT("OutlinerLevelUnknown", "<unknown>");
					}
				}))
		];
}

TArray<TSharedPtr<const UE::Editor::DataStorage::FColumnSorterInterface>> FOutlinerLevelWidgetConstructor::ConstructColumnSorters(
	UE::Editor::DataStorage::ICoreProvider* DataStorage,
	UE::Editor::DataStorage::IUiProvider* DataStorageUi,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	using namespace UE::Editor::DataStorage;
	return TArray<TSharedPtr<const FColumnSorterInterface>>(
		{
			MakeShared<FLevelWidgetSorter>()
		});
}

#undef LOCTEXT_NAMESPACE
