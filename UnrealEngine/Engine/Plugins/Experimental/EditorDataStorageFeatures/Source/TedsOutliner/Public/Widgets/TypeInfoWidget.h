// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"

#include "TypeInfoWidget.generated.h"

#define UE_API TEDSOUTLINER_API

namespace UE::Editor::DataStorage
{
	class FTypeInfoWidgetSorter final : public TColumnSorterInterface<FColumnSorterInterface::ESortType::HybridSort, FTypedElementClassTypeInfoColumn>
	{
	public:
		virtual FText GetShortName() const override;
	protected:
		virtual int32 Compare(const FTypedElementClassTypeInfoColumn& Left, const FTypedElementClassTypeInfoColumn& Right) const override;
		virtual FPrefixInfo CalculatePrefix(const FTypedElementClassTypeInfoColumn& Column, uint32 ByteIndex) const override;
	};
}

UCLASS()
class UTypeInfoWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypeInfoWidgetFactory() override = default;

	void RegisterWidgetConstructors(UE::Editor::DataStorage::ICoreProvider& DataStorage,
		UE::Editor::DataStorage::IUiProvider& DataStorageUi) const override;
};

USTRUCT()
struct FTypeInfoWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	UE_API FTypeInfoWidgetConstructor();
	~FTypeInfoWidgetConstructor() override = default;

protected:
	UE_API explicit FTypeInfoWidgetConstructor(const UScriptStruct* InTypeInfo);
	UE_API TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	UE_API bool FinalizeWidget(UE::Editor::DataStorage::ICoreProvider* DataStorage,
		UE::Editor::DataStorage::IUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
	UE_API TArray<TSharedPtr<const UE::Editor::DataStorage::FColumnSorterInterface>> ConstructColumnSorters(
		UE::Editor::DataStorage::ICoreProvider* DataStorage,
		UE::Editor::DataStorage::IUiProvider* DataStorageUi,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;

protected:

	// Whether the widget created by this constructor should be icon or text
	bool bUseIcon;
};

#undef UE_API
