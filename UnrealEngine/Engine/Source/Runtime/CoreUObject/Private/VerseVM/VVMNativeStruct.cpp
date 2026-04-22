// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMNativeStruct.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/TypeHash.h"
#include "UObject/UnrealType.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMClassInline.h"
#include "VerseVM/Inline/VVMNativeStructInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEmergentTypeCreator.h"
#include "VerseVM/VVMRef.h"
#include "VerseVM/VVMValue.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VNativeStruct);
TGlobalTrivialEmergentTypePtr<&VNativeStruct::StaticCppClassInfo> VNativeStruct::GlobalTrivialEmergentType;

template <typename TVisitor>
void VNativeStruct::VisitReferencesImpl(TVisitor& Visitor)
{
	const VEmergentType* EmergentType = GetEmergentType();

	// During serialization, EmergentType->Type is null and the body is uninitialized.
	if (!EmergentType->Type)
	{
		return;
	}

	// If this struct can contain references, queue it for a later visit by the UE ARO
	const VClass& Class = EmergentType->Type->StaticCast<VClass>();
	if (Class.IsNativeStructWithObjectReferences())
	{
		Visitor.MarkNativeStructAsReachable(this);
	}

	// Imported types do not have shapes.
	if (!EmergentType->Shape)
	{
		return;
	}

	// Visit the portion of this struct that is known to Verse
	void* Data = GetData(*EmergentType->CppClassInfo);
	for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
	{
		switch (It->Value.Type)
		{
			case EFieldType::FProperty:
				// C++ is responsible for tracing native fields.
				break;
			case EFieldType::FVerseProperty:
				Visitor.Visit(*It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data), TEXT(""));
				break;
			case EFieldType::Offset:
			case EFieldType::FPropertyVar:
			case EFieldType::Constant:
				VERSE_UNREACHABLE();
				break;
		}
	}
}

VNativeStruct& VNativeStruct::Duplicate(FAllocationContext Context)
{
	VEmergentType* EmergentType = GetEmergentType();
	UScriptStruct* ScriptStruct = GetUScriptStruct(*EmergentType);
	VNativeStruct& NewObject = VNativeStruct::NewUninitialized(Context, *EmergentType);
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);

	// TODO: AutoRTFM::Close and propagate any errors.
	ScriptStruct->CopyScriptStruct(NewData, Data);

	return NewObject;
}

ECompares VNativeStruct::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	// Since native structs carry blind C++ data, they can only be compared to the exact same type
	VEmergentType* EmergentType = GetEmergentType();
	if (EmergentType != Other->GetEmergentType())
	{
		return ECompares::Neq;
	}

	// Trust the C++ equality operator to do the right thing
	// TODO: This is wrong for non-native fields.
	UScriptStruct* ScriptStruct = GetUScriptStruct(*EmergentType);
	VNativeStruct& OtherStruct = Other->StaticCast<VNativeStruct>();

	// TODO: AutoRTFM::Close and propagate any errors.
	bool bResult = ScriptStruct->CompareScriptStruct(GetData(*EmergentType->CppClassInfo), OtherStruct.GetData(*EmergentType->CppClassInfo), PPF_None);
	return bResult ? ECompares::Eq : ECompares::Neq;
}

// TODO: Make this (And all other container TypeHash funcs) handle placeholders appropriately
uint32 VNativeStruct::GetTypeHashImpl()
{
	VEmergentType* EmergentType = GetEmergentType();
	UScriptStruct* ScriptStruct = GetUScriptStruct(*EmergentType);

	// TODO: AutoRTFM::Close and propagate any errors.
	if (UScriptStruct::ICppStructOps* CppStructOps = ScriptStruct->GetCppStructOps())
	{
		return CppStructOps->GetStructTypeHash(GetData(*EmergentType->CppClassInfo));
	}
	else
	{
		void* Data = GetData(*EmergentType->CppClassInfo);
		uint32 Result = 0;
		for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
		{
			switch (It->Value.Type)
			{
				case EFieldType::FProperty:
				case EFieldType::FVerseProperty:
					Result = ::HashCombineFast(It->Value.UProperty->GetValueTypeHash(It->Value.UProperty->ContainerPtrToValuePtr<void>(Data)));
					break;
				case EFieldType::Offset:
				case EFieldType::FPropertyVar:
				case EFieldType::Constant:
					VERSE_UNREACHABLE();
					break;
			}
		}
		return Result;
	}
}

VValue VNativeStruct::MeltImpl(FAllocationContext Context)
{
	// First make a native copy, then run the melt process on top of that
	VNativeStruct& NewObject = Duplicate(Context);

	// Now, do a second pass where we individually melt each VValue
	// Imported native structs may not have a shape, in which case Duplicate is sufficient.
	VEmergentType* EmergentType = GetEmergentType();
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);
	if (EmergentType->Shape)
	{
		for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
		{
			VValue MeltResult;
			switch (It->Value.Type)
			{
				case EFieldType::FProperty:
					// C++ copy constructor is responsible for melting native fields.
					break;
				case EFieldType::FVerseProperty:
					MeltResult = VValue::Melt(Context, It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Get(Context));
					if (MeltResult.IsPlaceholder())
					{
						return MeltResult;
					}
					It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(NewData)->Set(Context, MeltResult);
					break;
				case EFieldType::Offset:
				case EFieldType::FPropertyVar:
				case EFieldType::Constant:
					VERSE_UNREACHABLE();
					break;
			}
		}
	}

	return VValue(NewObject);
}

FOpResult VNativeStruct::FreezeImpl(FAllocationContext Context, VTask* Task, FOp* AwaitPC)
{
	// First make a native copy, then run the freeze process on top of that
	VNativeStruct& NewObject = Duplicate(Context);

	// Now, do a second pass where we individually freeze each VValue
	// Imported native structs may not have a shape, in which case Duplicate is sufficient.
	VEmergentType* EmergentType = GetEmergentType();
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);
	if (EmergentType->Shape)
	{
		for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
		{
			switch (It->Value.Type)
			{
				case EFieldType::FProperty:
					// C++ copy constructor is responsible for freezing native fields.
					break;
				case EFieldType::FVerseProperty:
				{
					VRestValue* FieldRestValue = It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data);
					VValue FieldValue = UnwrapTransparentRef(
						Context,
						FieldRestValue->Get(Context),
						Task,
						AwaitPC,
						[&](VValue FieldValue) { FieldRestValue->Set(Context, FieldValue); });
					FOpResult FrozenValue = VValue::Freeze(
						Context,
						FieldValue,
						Task,
						AwaitPC);
					V_DIE_UNLESS(FrozenValue.IsReturn()); // Verse properties should always contain valid data.
					It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(NewData)->Set(Context, FrozenValue.Value);
					break;
				}
				case EFieldType::Offset:
				case EFieldType::FPropertyVar:
				case EFieldType::Constant:
					VERSE_UNREACHABLE();
					break;
			}
		}
	}

	V_RETURN(NewObject);
}

void VNativeStruct::SerializeLayout(FAllocationContext Context, VNativeStruct*& This, FStructuredArchiveVisitor& Visitor)
{
	uint32 Size = 0;
	bool bHasDestructor = false;
	if (!Visitor.IsLoading())
	{
		VEmergentType* EmergentType = This->GetEmergentType();
		UScriptStruct* ScriptStruct = GetUScriptStruct(*EmergentType);
		Size = ScriptStruct->GetStructureSize();
		bHasDestructor = (ScriptStruct->StructFlags & STRUCT_NoDestructor) == 0;
	}

	Visitor.Visit(Size, TEXT("Size"));
	Visitor.Visit(bHasDestructor, TEXT("HasDestructor"));
	if (Visitor.IsLoading())
	{
		This = new (AllocateCell(Context, Size, bHasDestructor)) VNativeStruct(Context);
	}
}

void VNativeStruct::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	VClass* Class = nullptr;
	if (!Visitor.IsLoading())
	{
		VEmergentType* EmergentType = GetEmergentType();
		Class = &EmergentType->Type->StaticCast<VClass>();
	}

	Visitor.Visit(reinterpret_cast<VCell*&>(Class), TEXT("Class"));
	Class->GetUETypeChecked<UScriptStruct>()->SerializeItem(Visitor.Slot(TEXT("Fields")), GetStruct(), nullptr);
	if (Visitor.IsLoading())
	{
		VEmergentType& EmergentType = Class->GetOrCreateEmergentTypeForNativeStruct(Context);
		SetEmergentType(Context, &EmergentType);
	}
}

} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
