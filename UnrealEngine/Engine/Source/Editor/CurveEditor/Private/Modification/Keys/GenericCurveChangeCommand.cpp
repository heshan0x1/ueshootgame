// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericCurveChangeCommand.h"

#include "CurveEditor.h"
#include "Modification/Keys/GenericCurveChangeUtils.h"

namespace UE::CurveEditor
{
void FGenericCurveChangeCommand::Apply(UObject* Object)
{
	if (const TSharedPtr<FCurveEditor> CurveEditor = GetCurveEditor())
	{
		GenericCurveChange::ApplyChange(CurveEditor->GetCurves(), DeltaChange);
	}
}

void FGenericCurveChangeCommand::Revert(UObject* Object)
{
	if (const TSharedPtr<FCurveEditor> CurveEditor = GetCurveEditor())
	{
		GenericCurveChange::RevertChange(CurveEditor->GetCurves(), DeltaChange);
	}
}

SIZE_T FGenericCurveChangeCommand::GetSize() const
{
	return DeltaChange.GetAllocatedSize() + sizeof(FGenericCurveChangeCommand);
}

FString FGenericCurveChangeCommand::ToString() const
{
	return TEXT("FGenericCurveChangeCommand");
}
}
