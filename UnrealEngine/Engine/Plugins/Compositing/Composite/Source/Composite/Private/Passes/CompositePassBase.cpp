// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassBase.h"

#include "CompositeActor.h"

UCompositePassBase::UCompositePassBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Ensure instances are always transactional
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SetFlags(RF_Transactional);
	}

#if WITH_EDITOR
	DisplayName = GetClass()->GetName();
	
	const int32 NameNumber = GetFName().GetNumber();
	if (NameNumber > 1)
	{
		DisplayName.AppendInt(NameNumber);
	}
#endif
}

UCompositePassBase::~UCompositePassBase() = default;

bool UCompositePassBase::IsEnabled() const
{
	return bIsEnabled;
}

void UCompositePassBase::SetEnabled(bool bInEnabled)
{
	bIsEnabled = bInEnabled;
}

#if WITH_EDITOR
bool UCompositePassBase::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}
#endif //WITH_EDITOR

