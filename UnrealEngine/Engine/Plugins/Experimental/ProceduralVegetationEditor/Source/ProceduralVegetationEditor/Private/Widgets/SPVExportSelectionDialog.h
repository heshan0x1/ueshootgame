// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SWindow.h"

class UPCGEditorGraphNodeBase;
class UPVOutputSettings;
class SScrollBox;
class UProceduralVegetationGraph;
class UPCGNode;
class SPrimaryButton;
enum class EPVExportType;

class SPVExportSelectionDialog : public SWindow
{
	SLATE_BEGIN_ARGS(SPVExportSelectionDialog)
	{}
	SLATE_ARGUMENT(FText, Title)
	SLATE_ARGUMENT(const UProceduralVegetationGraph*, Graph)
	SLATE_ARGUMENT(TArray<FText>, ExportItems)
	SLATE_ARGUMENT(TArray<TObjectPtr<UPCGEditorGraphNodeBase>>, SelectedNodes)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	/** Displays the dialog in a blocking fashion */
	EAppReturnType::Type ShowModal();

protected:
	FReply OnButtonClick(EAppReturnType::Type ButtonID);
	EAppReturnType::Type UserResponse = EAppReturnType::Type::Cancel;
	
private:
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SPrimaryButton> ExportButton;
	const UProceduralVegetationGraph* Graph = nullptr;
	TArray<TObjectPtr<UPCGEditorGraphNodeBase>> SelectedNodes;

	TArray<TSharedPtr<FString>> ExportTypes;
	TSharedPtr<FString> CurrentExportType;

	EPVExportType GetExportType();
	
	void AddExportItems();
	void OnOutputSelectionChanged(const FString ItemName, ECheckBoxState NewState);

	void BuildExportTypeOptions();
	void OnExportTypeChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);

	void UpdateExportButtonState();
};
