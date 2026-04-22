// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/CompositeDataTableFactory.h"

#include "Engine/CompositeDataTable.h"
#include "Templates/SubclassOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CompositeDataTableFactory)

class UDataTable;
class UObject;

UCompositeDataTableFactory::UCompositeDataTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCompositeDataTable::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UDataTable* UCompositeDataTableFactory::MakeNewDataTable(UObject* InParent, FName Name, EObjectFlags Flags)
{
	return NewObject<UCompositeDataTable>(InParent, Name, Flags);
}
