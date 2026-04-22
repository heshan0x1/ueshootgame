// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMAccessor.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMValueObject.h"

// NOTE: (yiliang.siew) Silence these warnings for now in the cases below.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

namespace Verse
{

inline VValueObject& VValueObject::NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType)
{
	return *new (AllocateCell(Context, *InEmergentType.CppClassInfo, InEmergentType.Shape->NumIndexedFields)) VValueObject(Context, InEmergentType);
}

inline std::byte* VValueObject::AllocateCell(FAllocationContext Context, VCppClassInfo& CppClassInfo, uint64 NumIndexedFields)
{
	return Context.AllocateFastCell(DataOffset(CppClassInfo) + NumIndexedFields * sizeof(VRestValue));
}

inline VValueObject::VValueObject(FAllocationContext Context, VEmergentType& InEmergentType)
	: VObject(Context, InEmergentType)
{
	// We only need to allocate space for indexed fields since we are raising constants to the shape
	// and not storing their data on per-object instances.
	VRestValue* Data = GetFieldData(*InEmergentType.CppClassInfo);
	const uint64 NumIndexedFields = InEmergentType.Shape->NumIndexedFields;
	for (uint64 Index = 0; Index < NumIndexedFields; ++Index)
	{
		// We map whether this field has been created in the emergent shape
		new (&Data[Index]) VRestValue(0);
	}
}

inline bool VValueObject::CreateFieldCached(FAllocationContext Context, FCreateFieldCacheCase Cache)
{
	switch (Cache.Kind)
	{
		case FCreateFieldCacheCase::EKind::ValueObjectField:
		{
			bool bNewField = EmergentTypeOffset != Cache.NextEmergentTypeOffset;
			if (bNewField)
			{
				SetEmergentType(Context, FHeap::EmergentTypeOffsetToPtr(Cache.NextEmergentTypeOffset));
			}
			return bNewField;
		}
		default:
			return false;
	}
}

inline bool VValueObject::CreateField(FAllocationContext Context, VUniqueString& Name, FCreateFieldCacheCase* OutCacheCase)
{
	// TODO: (yiliang.siew) In the future, when the emergent type cache is not limited to the class, this will also need
	// to consider the type of the field, not just the name of the field itself, because a field being checked if it's
	// in the shape versus in the object should not be considered the same field when checking if it's already been
	// initialized.
	VEmergentType* EmergentType = GetEmergentType();
	V_DIE_UNLESS(EmergentType);
	const VShape* Shape = EmergentType->Shape.Get();
	V_DIE_UNLESS(Shape);
	FSetElementId Index = Shape->Fields.FindId({Context, Name});
	V_DIE_UNLESS(Index.IsValidId());
	const VShape::VEntry& Field = Shape->Fields.Get(Index).Value;
	// TODO: (yiliang.siew) We shouldn't be able to hit this today, but in the future when we allow adding fields dynamically
	// to objects, we should just return `false` if we don't have the field yet.
	V_DIE_IF_MSG(Field.IsProperty(), "`VValueObject::CreateField` was called for a native property: %s! This should be done through `VNativeConstructorWrapper::CreateField` instead!", *Name.AsString());

	if (Field.Type == EFieldType::Constant) // Field data lives in the shape, so we shouldn't bother running any initialization code here.
	{
		V_DIE_IF(Field.Value.Get().IsUninitialized());
		if (Field.Value.Get().IsCellOfType<VAccessor>())
		{
			int32 FieldIndex = Index.AsInteger();
			// proxy vars are stored as constants but still need to be marked like normal vars
			if (!EmergentType->IsFieldCreated(FieldIndex))
			{
				VEmergentType* NewType = EmergentType->MarkFieldAsCreated(Context, FieldIndex);
				SetEmergentType(Context, NewType);
				if (OutCacheCase)
				{
					OutCacheCase->Kind = FCreateFieldCacheCase::EKind::ValueObjectField;
					OutCacheCase->FieldIndex = FieldIndex;
					OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(NewType);
				}
				return true;
			}
		}
		if (OutCacheCase)
		{
			OutCacheCase->Kind = FCreateFieldCacheCase::EKind::ValueObjectConstant;
		}
		return false;
	}

	if (Field.Type == EFieldType::Offset) // Field data lives in the object
	{
		int32 FieldIndex = Index.AsInteger();
		if (EmergentType->IsFieldCreated(FieldIndex))
		{
			if (OutCacheCase)
			{
				OutCacheCase->Kind = FCreateFieldCacheCase::EKind::ValueObjectField;
				OutCacheCase->FieldIndex = FieldIndex;
				OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
			}
			return false;
		}

		VEmergentType* NewType = EmergentType->MarkFieldAsCreated(Context, FieldIndex);
		SetEmergentType(Context, NewType);
		if (OutCacheCase)
		{
			OutCacheCase->Kind = FCreateFieldCacheCase::EKind::ValueObjectField;
			OutCacheCase->FieldIndex = FieldIndex;
			OutCacheCase->NextEmergentTypeOffset = FHeap::EmergentTypePtrToOffset(NewType);
		}
		return true;
	}

	V_DIE("%s has an unsupported field type!", *Name.AsString());
}

} // namespace Verse
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif // WITH_VERSE_VM
