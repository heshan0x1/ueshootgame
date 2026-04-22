// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaStreamSource.h"

#include "MediaStreamSchemeHandlerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MediaStreamSource)

bool FMediaStreamSource::operator==(const FMediaStreamSource& InOther) const
{
	return Scheme == InOther.Scheme &&
		(Scheme.IsNone() || (Path.Equals(InOther.Path)));
}

TArray<FName> UMediaStreamSourceBlueprintFunctionLibrary::GetSchemeTypes()
{
	return FMediaStreamSchemeHandlerManager::Get().GetSchemeHandlerNames();
}
