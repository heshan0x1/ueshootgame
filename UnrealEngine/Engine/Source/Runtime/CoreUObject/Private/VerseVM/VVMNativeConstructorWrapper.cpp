// Copyright Epic Games, Inc. All Rights Reserved.

#if !WITH_VERSE_BPVM || defined(__INTELLISENSE__)

#include "VerseVM/VVMNativeConstructorWrapper.h"
#include "VerseVM/Inline/VVMMapInline.h"
#include "VerseVM/Inline/VVMNativeConstructorWrapperInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMNativeStruct.h"
#include "VerseVM/VVMValuePrinting.h"
#include "VerseVM/VVMVerseClass.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VNativeConstructorWrapper);
TGlobalTrivialEmergentTypePtr<&VNativeConstructorWrapper::StaticCppClassInfo> VNativeConstructorWrapper::GlobalTrivialEmergentType;

template <typename TVisitor>
void VNativeConstructorWrapper::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(NativeObject, TEXT("NativeObject"));
	Visitor.Visit(SelfPlaceholder, TEXT("SelfPlaceholder"));
	Visitor.Visit(FieldPlaceholders, TEXT("FieldPlaceholders"));
}

void VNativeConstructorWrapper::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	if (IsCellFormat(Format))
	{
		Builder << UTF8TEXT("Wrapped object(Value: %s");
		WrappedObject().AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);
		Builder << UTF8TEXT(")");
	}
}

VNativeConstructorWrapper& VNativeConstructorWrapper::New(FAllocationContext Context, VNativeStruct& ObjectToWrap)
{
	return *new (Context.AllocateFastCell(sizeof(VNativeConstructorWrapper))) VNativeConstructorWrapper(Context, ObjectToWrap);
}

VNativeConstructorWrapper& VNativeConstructorWrapper::New(FAllocationContext Context, UObject& ObjectToWrap)
{
	return *new (Context.AllocateFastCell(sizeof(VNativeConstructorWrapper))) VNativeConstructorWrapper(Context, ObjectToWrap);
}

VNativeConstructorWrapper::VNativeConstructorWrapper(FAllocationContext Context, VNativeStruct& NativeStruct)
	: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
	, SelfPlaceholder(0)
	, NativeObject(Context, NativeStruct)
{
	if (VShape* Shape = NativeStruct.GetEmergentType()->Shape.Get())
	{
		VBitMap BitMap(Context, Shape->GetMaxFieldIndex());
		SetEmergentType(Context, VEmergentType::New(Context, GetEmergentType(), &BitMap));
	}
}

VNativeConstructorWrapper::VNativeConstructorWrapper(FAllocationContext Context, UObject& UEObject)
	: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
	, SelfPlaceholder(0)
	, NativeObject(Context, &UEObject)
{
	if (UVerseClass* VerseClass = Cast<UVerseClass>(UEObject.GetClass()))
	{
		VShape& Shape = VerseClass->Shape.Get(Context).StaticCast<VShape>();
		VBitMap BitMap(Context, Shape.GetMaxFieldIndex());
		SetEmergentType(Context, VEmergentType::New(Context, GetEmergentType(), &BitMap));
	}
}
} // namespace Verse
#endif
