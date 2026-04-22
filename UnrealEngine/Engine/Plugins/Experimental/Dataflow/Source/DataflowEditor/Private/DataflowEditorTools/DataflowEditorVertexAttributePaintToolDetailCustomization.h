// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class IDetailCategoryBuilder;
class FSlimHorizontalToolBarBuilder;
class SWidget;
class UDataflowEditorVertexAttributePaintTool;
class UDataflowEditorVertexAttributePaintToolProperties;

namespace UE::Dataflow::Editor
{
	class FVertexAttributePaintToolDetailCustomization : public IDetailCustomization
	{
	public:

		virtual ~FVertexAttributePaintToolDetailCustomization() override;

		static TSharedRef<IDetailCustomization> MakeInstance();

		/** IDetailCustomization interface */
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

		void OnSelectionChanged();

	private:

		void AddEditingModeRow(IDetailCategoryBuilder& EditModeCategory) const;
		void AddColorModeRow(IDetailCategoryBuilder& EditModeCategory) const;
		void AddColorRampRow(IDetailCategoryBuilder& EditModeCategory) const;
		
		void AddBrushUI(IDetailLayoutBuilder& DetailBuilder) const;
		void AddBrushModeRow(IDetailCategoryBuilder& BrushCategory) const;
		void AddBrushFalloffModeRow(IDetailCategoryBuilder& BrushCategory) const;
		void AddBrushRadiusRow(IDetailCategoryBuilder& BrushCategory) const;
		void AddBrushValueRow(IDetailCategoryBuilder& BrushCategory) const;

		void AddSelectionUI(IDetailLayoutBuilder& DetailBuilder) const;
		TSharedRef<SWidget> MakeSelectionElementsToolbar() const;
		TSharedRef<SWidget> MakeSelectionIsolationWidget() const;
		TSharedRef<SWidget> MakeSelectionEditActionsToolbar() const;
		void AddSelectionElementsRow(IDetailCategoryBuilder& BrushCategory) const;
		void AddEmptySelectionWarningRow(IDetailCategoryBuilder& BrushCategory) const;
		void MakeSelectionOperationRow(IDetailCategoryBuilder& EditValuesCategory, const FText& RowName, const TSharedRef<SWidget>& ButtonWidget, const TSharedRef<SWidget>& ValueWidget) const;
		void MakeSelectionAddMultiplySliderOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionAddOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionReplaceOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionInvertOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionRelaxOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionMirrorOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionPruneOperationRow(IDetailCategoryBuilder& EditValuesCategory) const;
		void MakeSelectionCopyAndPasteRow(IDetailCategoryBuilder& EditValuesCategory) const;
		
		void HideProperty(IDetailLayoutBuilder& DetailBuilder, FName PropertyName) const;

		IDetailLayoutBuilder* CurrentDetailBuilder = nullptr;
		TWeakObjectPtr<UDataflowEditorVertexAttributePaintToolProperties> ToolProperties;
		TWeakObjectPtr<UDataflowEditorVertexAttributePaintTool> Tool;

		static float WeightSliderWidths;
		static float WeightEditingLabelsPercent;
		static float WeightEditVerticalPadding;
		static float WeightEditHorizontalPadding;
	};
}