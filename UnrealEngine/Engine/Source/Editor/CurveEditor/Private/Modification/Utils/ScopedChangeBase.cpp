// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modification/Utils/ScopedChangeBase.h"

#include "CoreGlobals.h"
#include "CurveEditor.h"

namespace UE::CurveEditor
{
FScopedChangeBase::FScopedChangeBase(TWeakPtr<FCurveEditor> InCurveEditor, const FText& InDescription)
	: WeakCurveEditor(MoveTemp(InCurveEditor))
	, Description(InDescription)
	, bWasStartedInTransaction(GUndo != nullptr)
{}

bool FScopedChangeBase::IsCancelled() const
{
	return bIsCancelled || (GUndo == nullptr && bWasStartedInTransaction);
}

void FScopedChangeBase::TryAppendCommand(TUniquePtr<FCurveEditorCommandChange> InCommand) const
{
	const TSharedPtr<FCurveEditor> CurveEditorPin = WeakCurveEditor.Pin();
	if (CurveEditorPin && !IsCancelled())
	{
		CurveEditorPin->GetTransactionManager()->AppendChange(MoveTemp(InCommand), Description);
	}
}
}
