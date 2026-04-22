// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerDragDrop.h"

#include "Common/GraphEditorSchemaActions.h"

#define LOCTEXT_NAMESPACE "VariableDragDropOp"

namespace UE::UAF::Editor
{

TSharedPtr<FVariableDragDropOp> FVariableDragDropOp::New(TSharedPtr<FAnimNextSchemaAction_Variable> InAction, TSharedPtr<FVariablesOutlinerEntryItem> InEntry)
{
	TSharedPtr<FVariableDragDropOp> NewOp = MakeShared<FVariableDragDropOp>();
	NewOp->WeakItem = InEntry;
	NewOp->SourceAction = InAction;
	NewOp->Construct();
	return NewOp;
}

TSharedPtr<FAnimNextSchemaAction_Variable> FVariableDragDropOp::GetAction() const
{
	if (SourceAction.IsValid())
	{
		return StaticCastSharedPtr<FAnimNextSchemaAction_Variable>(SourceAction);
	}

	return nullptr;
}

void FVariableDragDropOp::GetDefaultStatusSymbol(const FSlateBrush*& PrimaryBrushOut, FSlateColor& IconColorOut, FSlateBrush const*& SecondaryBrushOut, FSlateColor& SecondaryColorOut) const
{
	PrimaryBrushOut = StaticCastSharedPtr<FAnimNextSchemaAction_Variable>(SourceAction)->GetIconBrush();
	IconColorOut = StaticCastSharedPtr<FAnimNextSchemaAction_Variable>(SourceAction)->GetIconColor();
	SecondaryBrushOut = nullptr;
	SecondaryColorOut = FSlateColor::UseForeground();
}

TSharedPtr<FCategoryDragDropOp> FCategoryDragDropOp::New(TSharedPtr<FVariablesOutlinerCategoryItem> InEntry)
{
	TSharedPtr<FCategoryDragDropOp> NewOp = MakeShared<FCategoryDragDropOp>();
	NewOp->WeakItem = InEntry;

	NewOp->CurrentHoverText = FText::FromString(InEntry->CategoryName);
	NewOp->SetupDefaults();
	NewOp->Construct();
	
	return NewOp;
}

}

#undef LOCTEXT_NAMESPACE