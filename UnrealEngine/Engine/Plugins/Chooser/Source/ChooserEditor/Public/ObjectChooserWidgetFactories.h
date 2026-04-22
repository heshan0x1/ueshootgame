// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chooser.h"
#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "StructViewerModule.h"

#define UE_API CHOOSEREDITOR_API

class UChooserTable;
struct FChooserColumnBase;

namespace UE::ChooserEditor
{
	enum {  ColumnWidget_SpecialIndex_Header = -1 };
	enum {  ColumnWidget_SpecialIndex_Fallback = -2 };
	
	DECLARE_DELEGATE(FChooserWidgetValueChanged)
	typedef TFunction<TSharedRef<SWidget>(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ResultBaseClass, FChooserWidgetValueChanged)> FChooserWidgetCreator;
	typedef TFunction<TSharedRef<SWidget>(UChooserTable* Chooser, FChooserColumnBase* Column, int Row)> FColumnWidgetCreator;

	class FObjectChooserWidgetFactories
	{
	public:

		static UE_API TSharedPtr<SWidget> CreateWidget(bool bReadOnly, UObject* TransactionObject, UScriptStruct* DataBaseType, FInstancedStruct* Data, UClass* ResultBaseClass,
												FChooserWidgetValueChanged ValueChanged = FChooserWidgetValueChanged(), FText NullValueDisplayText = FText());
		
		static UE_API TSharedPtr<SWidget> CreateWidget(bool ReadOnly, UObject* TransactionObject, void* Value, const UStruct* ValueType, UClass* ResultBaseClass, FChooserWidgetValueChanged ValueChanged = FChooserWidgetValueChanged());
		static UE_API TSharedPtr<SWidget> CreateWidget(bool ReadOnly, UObject* TransactionObject, const UScriptStruct* BaseType, void* Value, const UStruct* ValueType, UClass* ResultBaseClass,
										const FOnStructPicked& CreateClassCallback, TSharedPtr<SBorder>* InnerWidget = nullptr, FChooserWidgetValueChanged ValueChanged = FChooserWidgetValueChanged(),
										FText NullValueDisplayText = FText());
		
		static UE_API TSharedPtr<SWidget> CreateColumnWidget(FChooserColumnBase* Column, const UStruct* ColumnType, UChooserTable* Chooser, int Row); // todo: chooser should be a UObject
		
		static UE_API void RegisterWidgets();

		static UE_API void RegisterWidgetCreator(const UStruct* Type, FChooserWidgetCreator Creator);
		static UE_API void RegisterColumnWidgetCreator(const UStruct* ColumnType, FColumnWidgetCreator Creator);
		
	private:
		static UE_API TMap<const UStruct*, FColumnWidgetCreator>  ColumnWidgetCreators;
		static UE_API TMap<const UStruct*, FChooserWidgetCreator> ChooserWidgetCreators;
	};
}

#undef UE_API
