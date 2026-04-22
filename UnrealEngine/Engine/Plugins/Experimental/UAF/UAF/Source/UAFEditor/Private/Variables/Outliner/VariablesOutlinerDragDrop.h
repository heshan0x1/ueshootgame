// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GraphEditorDragDropAction.h"
#include "ISceneOutlinerTreeItem.h"
#include "VariablesOutlinerEntryItem.h"
#include "VariablesOutlinerCategoryItem.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "UObject/WeakInterfacePtr.h"

class IAnimNextRigVMVariableInterface;
struct FAnimNextSchemaAction_Variable;
class UAnimNextRigVMAssetEntry;

namespace UE::UAF::Editor
{

class FVariableDragDropOp : public FGraphSchemaActionDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FVariableDragDropOp, FGraphSchemaActionDragDropAction)

	static TSharedPtr<FVariableDragDropOp> New(TSharedPtr<FAnimNextSchemaAction_Variable> InAction, TSharedPtr<FVariablesOutlinerEntryItem> InEntry);

	// FGraphSchemaActionDragDropAction interface
	virtual void GetDefaultStatusSymbol(const FSlateBrush*& PrimaryBrushOut, FSlateColor& IconColorOut, FSlateBrush const*& SecondaryBrushOut, FSlateColor& SecondaryColorOut) const override;

	TSharedPtr<FAnimNextSchemaAction_Variable> GetAction() const;

	TWeakPtr<FVariablesOutlinerEntryItem> WeakItem;
};

class FCategoryDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCategoryDragDropOp, FDecoratedDragDropOp)

	static TSharedPtr<FCategoryDragDropOp> New(TSharedPtr<FVariablesOutlinerCategoryItem> InEntry);

	TWeakPtr<FVariablesOutlinerCategoryItem> WeakItem;
};

}
