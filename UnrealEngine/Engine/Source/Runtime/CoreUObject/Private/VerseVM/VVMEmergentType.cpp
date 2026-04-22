// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMMapInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMAccessor.h"
#include "VerseVM/VVMAtomics.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMMap.h"
#include "VerseVM/VVMShape.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VEmergentType);

template <typename TVisitor>
void VEmergentType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Shape, TEXT("Shape"));
	Visitor.Visit(Type, TEXT("Type"));
	Visitor.Visit(MeltTransition, TEXT("MeltTransition"));
	Visitor.Visit(CreatedFields, TEXT("CreatedFields"));
	Visitor.Visit(CachedFieldTransitions, TEXT("CachedFieldTransitions"));
}

bool VEmergentType::Equals(FAllocationContext Context, const VEmergentType& EmergentType, const VEmergentType* Other, const VBitMap* InCreatedFields)
{
	return EmergentType.Shape == Other->Shape && EmergentType.Type == Other->Type && EmergentType.CppClassInfo == Other->CppClassInfo && InCreatedFields->Equals(Context, Other->CreatedFields);
}

VEmergentType& VEmergentType::GetOrCreateMeltTransitionSlow(FAllocationContext Context)
{
	if (Shape->NumIndexedFields == Shape->GetNumFields())
	{
		// We're good to go. No need to make anything.
		MeltTransition.Set(Context, this);
		return *this;
	}

	VShape& NewShape = Shape->CopyToMeltedShape(Context);
	V_DIE_UNLESS(NewShape.NumIndexedFields == NewShape.GetNumFields());

	VEmergentType* Transition = VEmergentType::New(Context, &NewShape, Type.Get(), CppClassInfo);
	Transition->MeltTransition.Set(Context, Transition); // The MeltTransition of a MeltTransition is itself.

	// The object should be done constructing before we expose it to the concurrent GC.
	StoreStoreFence();
	MeltTransition.Set(Context, Transition);
	return *Transition;
}

VEmergentType::VEmergentType(FAllocationContext Context, VShape* InShape, VEmergentType* EmergentType, VType* InType, VCppClassInfo* InCppClassInfo)
	: VCell(Context, EmergentType)
	, Shape(Context, InShape)
	, Type(Context, InType)
	, CppClassInfo(InCppClassInfo)
{
	if (Shape && (Shape->NumIndexedFields || Shape->bHasAccessors))
	{
		CreatedFields.Init(Context, Shape->GetMaxFieldIndex());
	}
}

VEmergentType::VEmergentType(FAllocationContext Context, const VEmergentType* Other, const VBitMap* InCreatedFields)
	: VCell(Context, VEmergentTypeCreator::EmergentTypeForEmergentType.Get())
	, Shape(Context, Other->Shape.Get())
	, Type(Context, Other->Type.Get())
	, CppClassInfo(Other->CppClassInfo)
	, CreatedFields(Context, *InCreatedFields)
{
}

bool VEmergentType::IsFieldCreated(uint32 FieldIndex)
{
	return CreatedFields.CheckBit(FieldIndex);
}

VEmergentType* VEmergentType::MarkFieldAsCreated(FAllocationContext Context, uint32 FieldIndex)
{
	if (!CachedFieldTransitions)
	{
		// init'd with size of 1 as my hunch is _most_ objects will be init'd in the same order each time and
		// thus will only ever have a single chain of emergent types they go through during initialization
		CachedFieldTransitions.Set(Context, VMapBase::New<VMap>(Context, 1));
	}

	if (VEmergentType* CachedType = CachedFieldTransitions->Find(Context, VInt(Context, FieldIndex)).DynamicCast<VEmergentType>())
	{
		return CachedType;
	}

	VBitMap NewCreatedFields = VBitMap(Context, CreatedFields);
	NewCreatedFields.SetBit(Context, FieldIndex);

	// The original emergent type for our object is stored, and its chain of 'CachedFieldTransitions'
	// will cache new types we make to track created fields so we don't keep re-creating them
	VEmergentType* NewType = VEmergentType::New(Context, this, &NewCreatedFields);
	CachedFieldTransitions->Add(Context, VInt(Context, FieldIndex), *NewType);
	return NewType;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
