// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "UObject/VerseValueProperty.h"
#include "VerseVM/VVMAccessor.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMNativeConstructorWrapper.h"
#include "VerseVM/VVMNativeConverter.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMNativeRef.h"
#include "VerseVM/VVMVerseClass.h"

inline Verse::FOpResult UVerseClass::LoadField(Verse::FAllocationContext Context, UObject* Object, Verse::VUniqueString& FieldName, Verse::VNativeConstructorWrapper* Wrapper)
{
	using namespace Verse;

	UVerseClass* Class = CastChecked<UVerseClass>(Object->GetClass());
	VShape& Shape = Class->Shape.Get(Context).StaticCast<VShape>();
	const VShape::VEntry* Field = Shape.GetField(FieldName);

	switch (Field->Type)
	{
		case EFieldType::FProperty:
			return VNativeRef::Get(Context, Object, Field->UProperty);
		case EFieldType::FPropertyVar:
			V_RETURN(VNativeRef::New(Context, Object, Field->UProperty));
		case EFieldType::FVerseProperty:
		{
			VRestValue& Slot = *Field->UProperty->ContainerPtrToValuePtr<VRestValue>(Object);

			if (LIKELY(Slot.CanDefQuickly()) && GUObjectArray.IsDisregardForGC(Object))
			{
				VValue Placeholder = Slot.Get(Context);
				Placeholder.AsPlaceholder().AddRef(Context);
				V_RETURN(Placeholder);
			}

			V_RETURN(Slot.Get(Context));
		}
		case EFieldType::Constant:
		{
			VValue FieldValue = Field->Value.Get();
			V_DIE_IF(FieldValue.IsCellOfType<VProcedure>());
			// TODO: Bind to Wrapper?
			if (VFunction* Function = FieldValue.DynamicCast<VFunction>(); Function && !Function->HasSelf())
			{
				// NOTE: (yiliang.siew) Update the function-without-`Self` to point to the current object instance.
				// We only do this if the function doesn't already have a `Self` bound - in the case of fields that
				// are pointing to functions, we don't want to overwrite that `Self` which was already previously-bound.
				V_RETURN(Function->Bind(Context, Object));
			}
			else if (VNativeFunction* NativeFunction = FieldValue.DynamicCast<VNativeFunction>(); NativeFunction && !NativeFunction->HasSelf())
			{
				V_RETURN(NativeFunction->Bind(Context, Object));
			}
			else if (VAccessor* Accessor = FieldValue.DynamicCast<VAccessor>())
			{
				if (Wrapper)
				{
					V_RETURN(VAccessChain::New(Context, Accessor, *Wrapper));
				}
				else
				{
					V_RETURN(VAccessChain::New(Context, Accessor, Object));
				}
			}
			V_RETURN(FieldValue);
		}
		case EFieldType::Offset:
		default:
			VERSE_UNREACHABLE();
	}
}

#endif // WITH_VERSE_VM
