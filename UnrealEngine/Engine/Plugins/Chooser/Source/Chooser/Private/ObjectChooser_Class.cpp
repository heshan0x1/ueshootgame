// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooser_Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ObjectChooser_Class)

UObject* FClassChooser::ChooseObject(FChooserEvaluationContext& Context) const
{
	return Class;
}

FObjectChooserBase::EIteratorStatus FClassChooser::IterateObjects(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const
{
	return Callback.Execute(Class);
}
