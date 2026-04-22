// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureMetadata.h"

#include "UObject/Package.h"

#if WITH_EDITOR
#include "EditorDialogLibrary.h"
#endif

void UCaptureMetadata::SetCaptureMetadata(UObject* InObject, const UCaptureMetadata* InCaptureMetadata)
{
	if (!IsValid(InObject))
	{
		return;
	}

	if (!IsValid(InCaptureMetadata))
	{
		return;
	}

	FMetaData& Metadata = InObject->GetPackage()->GetMetaData();

	TFieldIterator<FProperty> It(InCaptureMetadata->GetClass());

	bool bIsEdited = false;

	while (It)
	{
		FProperty* Property = *It;
		check(Property->IsA<FStrProperty>());

		const FStrProperty* StringProperty = CastField<FStrProperty>(Property);

		const void* ValueAddr = Property->ContainerPtrToValuePtr<void>(InCaptureMetadata);

		FString Value = StringProperty->GetPropertyValue(ValueAddr);

		FString OldValue;
		if (Metadata.HasValue(InObject, Property->GetFName()))
		{
			OldValue = Metadata.GetValue(InObject, Property->GetFName());
		}

		if (Value != OldValue)
		{
			Metadata.SetValue(InObject, Property->GetFName(), *Value);

			bIsEdited = true;
		}
		
		++It;
	}

	if (bIsEdited)
	{
		InObject->MarkPackageDirty();
	}
}

UCaptureMetadata* UCaptureMetadata::GetCaptureMetadata(const UObject* InObject)
{
	if (!IsValid(InObject))
	{
		return nullptr;
	}

	FMetaData& Metadata = InObject->GetPackage()->GetMetaData();

	UCaptureMetadata* CaptureMetadata = NewObject<UCaptureMetadata>(GetTransientPackage(), InObject->GetFName());
	CaptureMetadata->OwnerName = InObject->GetFName().ToString();

	TMap<FName, FString>* MetadataMap = Metadata.GetMapForObject(InObject);
	if (!MetadataMap)
	{
		return CaptureMetadata;
	}

	for (const TPair<FName, FString>& MetadataPair : *MetadataMap)
	{
		FProperty* Property = CaptureMetadata->GetClass()->FindPropertyByName(MetadataPair.Key);

		if (Property)
		{
			check(Property->IsA<FStrProperty>());

			const FStrProperty* StringProperty = CastField<FStrProperty>(Property);
			void* ValueAddr = Property->ContainerPtrToValuePtr<void>(CaptureMetadata);

			StringProperty->SetPropertyValue(ValueAddr, MetadataPair.Value);
		}
	}

	return CaptureMetadata;
}

void UCaptureMetadata::ClearCaptureMetadata(const UObject* InObject)
{
	if (!IsValid(InObject))
	{
		return;
	}

	FMetaData& Metadata = InObject->GetPackage()->GetMetaData();

	TMap<FName, FString>* MetadataMap = Metadata.GetMapForObject(InObject);
	if (!MetadataMap)
	{
		return;
	}

	TMap<FName, FString> MetadataCopy = *MetadataMap;

	bool bIsEdited = false;
	for (const TPair<FName, FString>& MetadataPair : MetadataCopy)
	{
		FProperty* Property = UCaptureMetadata::StaticClass()->FindPropertyByName(MetadataPair.Key);

		if (Property)
		{
			check(Property->IsA<FStrProperty>());

			if (Metadata.HasValue(InObject, MetadataPair.Key))
			{
				Metadata.RemoveValue(InObject, MetadataPair.Key);

				bIsEdited = true;
			}
		}
	}

	if (bIsEdited)
	{
		InObject->MarkPackageDirty();
	}
}

bool UCaptureMetadata::ShowCaptureMetadataObjects(const FText& InTitle, const TArray<UObject*>& InObjects, const FCaptureMetadataWindowOptions& InOptions)
{
#if WITH_EDITOR
	for (UObject* Object : InObjects)
	{
		UCaptureMetadata* CaptureMetadata = StaticCast<UCaptureMetadata*>(Object);
		CaptureMetadata->bIsEditable = InOptions.bAllowEdit;
	}

	FEditorDialogLibraryObjectDetailsViewOptions Options;
	Options.bShowObjectName = false;
	Options.bAllowResizing = true;
	Options.MinWidth = 400;
	Options.MinHeight = 200;

	return UEditorDialogLibrary::ShowObjectsDetailsView(InTitle, InObjects, Options);
#else
	return false;
#endif
}

bool UCaptureMetadata::IsEditable() const
{
	return bIsEditable;
}

FString UCaptureMetadata::GetOwnerName() const
{
	return OwnerName;
}

void UCaptureMetadata::PostInitProperties()
{
	Super::PostInitProperties();

	// This function ensures that all properties defined in the UCaptureMetadata class are of type FString
	TFieldIterator<FProperty> It(GetClass());

	while (It)
	{
		FProperty* Property = *It;
		check(Property->IsA<FStrProperty>());
		++It;
	}
}