// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMNativeStructInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/Inline/VVMVerseClassInline.h"
#include "VerseVM/VVMNativeConstructorWrapper.h"
#include "VerseVM/VVMUniqueString.h"

namespace Verse
{

inline bool VNativeConstructorWrapper::CreateFieldCached(FAllocationContext Context, FCreateFieldCacheCase Cache)
{
	bool bNewField = EmergentTypeOffset != Cache.NextEmergentTypeOffset;
	switch (Cache.Kind)
	{
		case FCreateFieldCacheCase::EKind::NativeStruct:
		{
			VNativeStruct& NativeStruct = WrappedObject().template StaticCast<VNativeStruct>();
			VShape* Shape = NativeStruct.GetEmergentType()->Shape.Get();
			const VShape::VEntry& Field = Shape->GetField(Cache.FieldIndex);
			if (bNewField)
			{
				SetEmergentType(Context, FHeap::EmergentTypeOffsetToPtr(Cache.NextEmergentTypeOffset));
				if (Field.IsProperty())
				{
					void* Data = NativeStruct.GetData(*NativeStruct.GetEmergentType()->CppClassInfo);
					Field.UProperty->InitializeValue_InContainer(Data);
					if (Field.Type == EFieldType::FVerseProperty)
					{
						Field.UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Reset(0);
					}
				}
			}
			break;
		}
		case FCreateFieldCacheCase::EKind::UObject:
		{
			UObject* UEObject = WrappedObject().ExtractUObject();
			UVerseClass* VerseClass = Cast<UVerseClass>(UEObject->GetClass());
			VShape& Shape = VerseClass->Shape.Get(Context).StaticCast<VShape>();
			const VShape::VEntry& Field = Shape.GetField(Cache.FieldIndex);
			if (bNewField)
			{
				SetEmergentType(Context, FHeap::EmergentTypeOffsetToPtr(Cache.NextEmergentTypeOffset));
				if (Field.IsProperty())
				{
					Field.UProperty->InitializeValue_InContainer(UEObject);
					if (Field.Type == EFieldType::FVerseProperty)
					{
						Field.UProperty->ContainerPtrToValuePtr<VRestValue>(UEObject)->Reset(0);
					}
				}
			}
			break;
		}
	}
	return bNewField;
}

inline bool VNativeConstructorWrapper::CreateField(FAllocationContext Context, VUniqueString& FieldName, FCreateFieldCacheCase* OutCacheCase)
{
	if (VNativeStruct* NativeStruct = WrappedObject().template DynamicCast<VNativeStruct>())
	{
		if (VShape* Shape = NativeStruct->GetEmergentType()->Shape.Get())
		{
			FSetElementId FieldIndex = Shape->Fields.FindId({Context, FieldName});
			const VShape::VEntry& Field = Shape->Fields.Get(FieldIndex).Value;
			if (Field.IsProperty() || Field.IsAccessor())
			{
				if (IsFieldCreated(FieldIndex.AsInteger()))
				{
					if (OutCacheCase)
					{
						OutCacheCase->Kind = FCreateFieldCacheCase::EKind::NativeStruct;
						OutCacheCase->FieldIndex = FieldIndex.AsInteger();
						OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(GetEmergentType());
					}
					return false;
				}
				VEmergentType* NewType = GetEmergentType()->MarkFieldAsCreated(Context, FieldIndex.AsInteger());
				SetEmergentType(Context, NewType);
				if (OutCacheCase)
				{
					OutCacheCase->Kind = FCreateFieldCacheCase::EKind::NativeStruct;
					OutCacheCase->FieldIndex = FieldIndex.AsInteger();
					OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(NewType);
				}

				if (Field.IsProperty())
				{
					void* Data = NativeStruct->GetData(*NativeStruct->GetEmergentType()->CppClassInfo);
					Field.UProperty->InitializeValue_InContainer(Data);
					if (Field.Type == EFieldType::FVerseProperty)
					{
						Field.UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Reset(0);
					}
				}
				return true;
			}
		}
	}
	else if (UObject* UEObject = WrappedObject().ExtractUObject(); UEObject)
	{
		if (UVerseClass* VerseClass = Cast<UVerseClass>(UEObject->GetClass()))
		{
			VShape& Shape = VerseClass->Shape.Get(Context).StaticCast<VShape>();
			FSetElementId FieldIndex = Shape.Fields.FindId({Context, FieldName});
			const VShape::VEntry& Field = Shape.Fields.Get(FieldIndex).Value;
			if (Field.IsProperty() || Field.IsAccessor())
			{
				if (IsFieldCreated(FieldIndex.AsInteger()))
				{
					if (OutCacheCase)
					{
						OutCacheCase->Kind = FCreateFieldCacheCase::EKind::UObject;
						OutCacheCase->FieldIndex = FieldIndex.AsInteger();
						OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(GetEmergentType());
					}
					return false;
				}
				VEmergentType* NewType = GetEmergentType()->MarkFieldAsCreated(Context, FieldIndex.AsInteger());
				SetEmergentType(Context, NewType);
				if (OutCacheCase)
				{
					OutCacheCase->Kind = FCreateFieldCacheCase::EKind::UObject;
					OutCacheCase->FieldIndex = FieldIndex.AsInteger();
					OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(NewType);
				}

				if (Field.IsProperty())
				{
					Field.UProperty->InitializeValue_InContainer(UEObject);
					if (Field.Type == EFieldType::FVerseProperty)
					{
						Field.UProperty->ContainerPtrToValuePtr<VRestValue>(UEObject)->Reset(0);
					}
				}
				return true;
			}
		}
	}
	V_DIE("Could not create field for imported class/struct!");
	return false;
}

inline int32 VNativeConstructorWrapper::GetFieldIndex(FAllocationContext Context, VUniqueString& FieldName)
{
	if (VNativeStruct* NativeStruct = WrappedObject().DynamicCast<VNativeStruct>())
	{
		VEmergentType* EmergentType = NativeStruct->GetEmergentType();
		if (VShape* Shape = EmergentType->Shape.Get())
		{
			const VShape::VEntry* Field = Shape->GetField(FieldName);
			if (Field->IsProperty() || Field->IsAccessor())
			{
				return Shape->GetFieldIndex(Context, FieldName);
			}
		}
	}
	else if (UObject* UEObject = WrappedObject().ExtractUObject(); UEObject)
	{
		if (UVerseClass* VerseClass = Cast<UVerseClass>(UEObject->GetClass()))
		{
			VShape& Shape = VerseClass->Shape.Get(Context).StaticCast<VShape>();
			const VShape::VEntry* Field = Shape.GetField(FieldName);
			if (Field->IsProperty() || Field->IsAccessor())
			{
				return Shape.GetFieldIndex(Context, FieldName);
			}
		}
	}
	return -1;
}

inline VValue VNativeConstructorWrapper::WrappedObject() const
{
	return NativeObject.Get();
}
} // namespace Verse
#endif // WITH_VERSE_VM
