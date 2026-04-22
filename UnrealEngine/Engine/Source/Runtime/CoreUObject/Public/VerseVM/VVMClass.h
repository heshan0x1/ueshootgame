// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/StringFwd.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMNamedType.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMShape.h"

class UObject;
class UVerseClass;
class UVerseStruct;

namespace Verse
{
enum class EValueStringFormat;
struct FAbstractVisitor;
struct VObject;
struct VValueObject;
struct VNativeStruct;
struct VProcedure;
struct VPackage;
struct VFunction;
struct VScope;
struct VNativeConstructorWrapper;
struct FInitOrValidateUVerseClass;
struct FInitOrValidateUVerseStruct;
struct FInitOrValidateUStruct;

/// This provides a custom comparison that allows us to do pointer-based compares of each unique string set, rather than hash-based comparisons.
struct FEmergentTypesCacheKeyFuncs : TDefaultMapKeyFuncs<TWriteBarrier<VUniqueStringSet>, TWriteBarrier<VEmergentType>, /*bInAllowDuplicateKeys*/ false>
{
public:
	static bool Matches(KeyInitType A, KeyInitType B);
	static bool Matches(KeyInitType A, const VUniqueStringSet& B);
	static uint32 GetKeyHash(KeyInitType Key);
	static uint32 GetKeyHash(const VUniqueStringSet& Key);
};

enum class EArchetypeEntryFlags : uint8
{
	None = 0,
	/// If the entry in the archetype requires a native property type.
	NativeRepresentation = 1 << 0,
	/// If the entry in the archetype will be default-initialized by an expression.
	HasDefaultValueExpression = 1 << 1,
	/// If the field's FProperty should have the CPF_InstancedReference flag.
	Instanced = 1 << 2,
	/// If the field's FProperty for this used the entry's qualified name
	UseCRCName = 1 << 3,
	/// If the field has the <predicts> attribute (for data) or effect (for functions)
	Predicts = 1 << 4,
	/// If the field is a var
	Var = 1 << 5
};
ENUM_CLASS_FLAGS(EArchetypeEntryFlags)

/// Represents the sequence of fields in a class body, a constructor function or an archetype instantiation.
struct VArchetype : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	struct VEntry
	{
		static VEntry Constant(FAllocationContext Context, VUniqueString& InQualifiedField, VValue InType, VValue InValue);
		static VEntry Field(FAllocationContext Context, VUniqueString& InQualifiedField, VValue InType);
		static VEntry InitializedField(FAllocationContext Context, VUniqueString& InQualifiedField, VValue InType);

		static VEntry ObjectField(FAllocationContext Context, VUniqueString& InQualifiedField);

		bool IsConstant() const;
		bool IsNativeRepresentation() const;
		bool UseCRCName() const;
		bool HasDefaultValueExpression() const;
		bool IsInstanced() const;
		bool IsPredicts() const;
		bool IsVar() const;

		/// Checks if the entry refers to a method that is unbound (i.e. has no `Self`).
		COREUOBJECT_API bool IsMethod() const;

		TWriteBarrier<VUniqueString> Name;

		/// For data members, the declared type.
		TWriteBarrier<VValue> Type;

		/*
		  `Value` should be one of the following:

		  - An uninitialized `VValue`, for a field initializer (regardless of whether or not the field is actually uninitialized).

		  - A constant `VValue` representing a default field value. (This may be a `VFunction` without the `Self` member
			for methods, since they will bind `Self` lazily).
		 */
		TWriteBarrier<VValue> Value;

		EArchetypeEntryFlags Flags;
	};

	/// The class that this archetype initializes. Not set for operands of NewObject.
	/// If `this == Class->Archetype`, then this archetype describes the class body itself.
	/// This is a VRestValue because it can be non-concrete during module top-level initialization.
	VRestValue Class{0};

	/// The remaining chain of archetypes that this archetype overrides.
	/// For operands of NewObject, only set if the archetype calls a constructor function.
	/// If this archetype describes a class body, then this is the archetype of the superclass.
	/// This is a VRestValue because it can be non-concrete during module top-level initialization.
	VRestValue NextArchetype{0};

	/// The number of entries in this constructor object.
	uint32 NumEntries;
	VEntry Entries[];

	static VArchetype& New(FAllocationContext Context, VValue InNextArchetype, const TArray<VEntry>& InEntries);

	COREUOBJECT_API void AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth);

	static void SerializeLayout(FAllocationContext Context, VArchetype*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

	/// This loads the method that matches the given name. If it does not exist, returns `VValue()`.
	COREUOBJECT_API VValue LoadFunction(FAllocationContext Context, VUniqueString& FieldName, VValue SelfObject);

private:
	static VArchetype& NewUninitialized(FAllocationContext Context, const uint32 InNumEntries)
	{
		size_t NumBytes = offsetof(VArchetype, Entries) + InNumEntries * sizeof(Entries[0]);
		return *new (Context.AllocateFastCell(NumBytes)) VArchetype(Context, InNumEntries);
	}

	VArchetype(FAllocationContext Context, VValue InNextArchetype, const TArray<VEntry>& InEntries);
	VArchetype(FAllocationContext Context, const uint32 InNumEntries);

	friend struct VClass;
};

struct VClass : VNamedType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VNamedType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	enum class EKind : uint8
	{
		Class,
		Struct,
		Interface
	};
	enum class EFlags : uint16
	{
		None = 0,
		NativeRepresentation = 1 << 0,
		NativeStructWithObjectReferences = 1 << 1,
		Concrete = 1 << 2,
		FinalSuper = 1 << 3,
		ExplicitlyCastable = 1 << 4,
		Parametric = 1 << 5,
		UniversallyAccessible = 1 << 6,
		EpicInternal = 1 << 7,
		VNIEpicInternal = 1 << 8,
		Predicts = 1 << 9,
	};

	EKind GetKind() const { return Kind; }
	bool IsStruct() const { return GetKind() == EKind::Struct; }
	bool IsInterface() const { return GetKind() == EKind::Interface; }

	bool IsNativeRepresentation() const { return EnumHasAllFlags(Flags, EFlags::NativeRepresentation); }
	bool IsNativeStruct() const { return IsNativeRepresentation() && IsStruct(); }
	bool IsNativeStructWithObjectReferences() const { return EnumHasAllFlags(Flags, EFlags::NativeStructWithObjectReferences); }
	bool IsPredicts() const { return EnumHasAllFlags(Flags, EFlags::Predicts); }

	TArrayView<TWriteBarrier<VClass>> GetInherited() { return {Inherited, NumInherited}; }
	VArchetype& GetArchetype() const { return *Archetype.Get(); }
	VFunction& GetConstructor() const { return *Constructor.Get(); }

	/// Allocates a new `VValueObject` that has enough space to accommodate the entries in the archetype.
	COREUOBJECT_API VValueObject& NewVObject(FAllocationContext Context, VArchetype& InArchetype);
	/// Allocates a new `VValueObject` of a known emergent type.
	COREUOBJECT_API VValueObject& NewVObjectOfEmergentType(FAllocationContext Context, VEmergentType& EmergentType);

	COREUOBJECT_API VNativeConstructorWrapper& NewNativeStruct(FAllocationContext Context);

	/// Allocate a new VNativeStruct and move an existing struct into it
	template <class CppStructType>
	VNativeStruct& NewNativeStruct(FAllocationContext Context, CppStructType&& Struct);

	/// Allocate a new `UObject`, wrapped in a data structure that tracks which fields have been initialized.
	COREUOBJECT_API VNativeConstructorWrapper& NewUObject(FAllocationContext Context, VArchetype& InArchetype);

	VEmergentType& GetOrCreateEmergentTypeForVObject(FAllocationContext Context, VCppClassInfo* CppClassInfo, VArchetype& InArchetype);
	COREUOBJECT_API VEmergentType& GetOrCreateEmergentTypeForNativeStruct(FAllocationContext Context);
	COREUOBJECT_API UStruct* GetOrCreateNativeType(FAllocationContext Context);

	/**
	   Creates a new class.

	   @param InPackageScope        Containing package or null.
	   @param InRelativePath        Path within containing package.
	   @param InClassName           Unqualified name.
	   @param InAttributeIndices
	   @param InAttributes
	   @param InImportStruct        The Unreal class/struct that is being reflected by this Verse VM type.
	   @param bNativeBound
	   @param InKind                Class, Struct or Interface.
	   @param InFlags
	   @param InInherited           An array of base classes, in order of inheritance.
	   @param InArchetype           The sequence of fields and blocks in the class body.
	   @param InConstructor         The class body's bytecode that initializes its fields.
	 */
	static VClass& New(
		FAllocationContext Context,
		VPackage* InPackageScope,
		VArray* InRelativePath,
		VArray* InClassName,
		VArray* InAttributeIndices,
		VArray* InAttributes,
		UStruct* InImportStruct,
		bool bNativeBound,
		EKind InKind,
		EFlags InFlags,
		const TArray<VClass*>& InInherited,
		VArchetype& InArchetype,
		VProcedure& InConstructor);

	bool SubsumesImpl(FAllocationContext, VValue);

	static void SerializeLayout(FAllocationContext Context, VClass*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

	COREUOBJECT_API TSharedPtr<FJsonValue> ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs);
	COREUOBJECT_API VValue FromJSONImpl(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format);

private:
	COREUOBJECT_API VClass(
		FAllocationContext Context,
		VPackage* InPackageScope,
		VArray* InRelativePath,
		VArray* InClassName,
		VArray* InAttributeIndices,
		VArray* InAttributes,
		UStruct* InImportStruct,
		bool bInNativeBound,
		EKind InKind,
		EFlags InFlags,
		const TArray<VClass*>& InInherited,
		VArchetype& InArchetype,
		VProcedure& InConstructor);

	COREUOBJECT_API VClass(FAllocationContext Context, int32 NumInherited);

	/// Walks a chain of archetypes recursively, calling FieldCallbackProc for each entry.
	/// BaseIndex is added to the entry indices passed to FieldCallbackProc; initial callers probably just want 0.
	static void WalkArchetypeFields(
		FAllocationContext Context,
		VArchetype& InArchetype,
		VClass& InClass,
		VArchetype* InNextArchetype,
		int32& BaseIndex,
		TFunction<void(int32&)> WalkSuper,
		TFunction<void(VArchetype::VEntry&, int32, VClass&)> FieldCallbackProc);

	/// Creates an associated UClass or UScriptStruct for this VClass
	VShape* BindNativeClass(FAllocationContext Context, bool bImported);

	void CommonPrepare(FAllocationContext Context, FInitOrValidateUStruct& InitOrValidate, UStruct* Type);
	void Prepare(FAllocationContext Context, FInitOrValidateUVerseStruct& InitOrValidate, UVerseStruct* Type);
	void Prepare(FAllocationContext Context, FInitOrValidateUVerseClass& InitOrValidate, UVerseClass* Type);

	VShape* CreateShapeForUStruct(
		FAllocationContext Context,
		TFunction<FProperty*(VArchetype::VEntry&, int32)>&& CreateProperty,
		TFunction<UFunction*(VArchetype::VEntry&)>&& CreateFunction);
	VShape* CreateShapeForExistingUStruct(FAllocationContext Context);
	UFunction* MaybeCreateUFunctionForCallee(Verse::FAllocationContext Context, VValue Callee);

	void ConstructNativeDefaultObject(FRunningContext Context, bool bRequireConcreteEffectToken);
	static FOpResult MarkSuperFieldsCreated(FRunningContext Context, VValue Self, TArrayView<VValue> Arguments);

	EKind Kind;
	EFlags Flags;

	// Super classes and interfaces. The single superclass is always first.
	int32 NumInherited;

	/// Represents the fields in the class. May contain entries that point to other delegating archetypes (if this is a
	/// subclass).
	TWriteBarrier<VArchetype> Archetype;

	/// This is the body function (wrapped with `(super:)`) that contains the bytecode which handles initialization of
	/// the class fields.
	TWriteBarrier<VFunction> Constructor;

	// TODO: (yiliang.siew) This should be a weak map when we can support it in the GC. https://jira.it.epicgames.com/browse/SOL-5312
	/// This is a cache that allows for fast vending of emergent types based on the fields being overridden.
	/// When this is made weak, `VProcedure::VisitReferencesImpl` must be updated to keep OpNewObjectICArchetype's cached emergent type alive.
	TMap<TWriteBarrier<VUniqueStringSet>, TWriteBarrier<VEmergentType>, FDefaultSetAllocator, FEmergentTypesCacheKeyFuncs> EmergentTypesCache;

	/// The in-order list of superclasses that this class inherits from.
	TWriteBarrier<VClass> Inherited[];

	friend class FInterpreter;
	friend ::UVerseClass;
	friend ::UVerseStruct;
};

ENUM_CLASS_FLAGS(VClass::EFlags)
} // namespace Verse
#endif // WITH_VERSE_VM
