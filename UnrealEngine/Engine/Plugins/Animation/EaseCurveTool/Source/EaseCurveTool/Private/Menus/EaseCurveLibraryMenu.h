// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"
#include "Styling/SlateTypes.h"

class FExtender;
class FMenuBuilder;
class UEaseCurveLibrary;
class UEaseCurveSerializer;
class UToolMenu;
struct FAssetData;

namespace UE::EaseCurveTool
{

class FEaseCurveLibraryMenu : public TSharedFromThis<FEaseCurveLibraryMenu>
{
public:
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& InSelectedAssets);

	static void AddMenuImportExportSection(FMenuBuilder& InMenuBuilder
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);

protected:
	static void AddMenuEntryForNoSerializers(FMenuBuilder& InMenuBuilder);

	static bool AddMenuEntryForSerializer(FMenuBuilder& InMenuBuilder
		, UEaseCurveSerializer& InSerializer
		, const bool bInImport
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);

	static void PopulateImportMenu(FMenuBuilder& InMenuBuilder
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);
	static void PopulateExportMenu(FMenuBuilder& InMenuBuilder
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);

	static void PromptForImport(UEaseCurveSerializer* const InSerializer
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);
	static void PromptForExport(UEaseCurveSerializer* const InSerializer
		, const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);

	static void SetToDefaultPresets(const TSet<TWeakObjectPtr<UEaseCurveLibrary>> InWeakLibraryAssets);
};

} // namespace UE::EaseCurveTool
