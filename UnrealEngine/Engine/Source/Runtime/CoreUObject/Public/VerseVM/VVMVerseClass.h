// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/AnsiString.h"
#include "Misc/NotNull.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/ScriptMacros.h"
#include "VerseVM/VVMVerseClassFlags.h"
#include "VerseVM/VVMVerseConstraints.h"
#include "VerseVM/VVMVerseEffectSet.h"
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMShape.h"
#endif

#include "VVMVerseClass.generated.h"

class UClassCookedMetaData;
struct FVniTypeDesc;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
namespace Verse
{
struct VNativeConstructorWrapper;
struct VPackage;
} // namespace Verse
#endif

USTRUCT()
struct FVersePersistentVar
{
	GENERATED_BODY()

	FVersePersistentVar(FString Path, TFieldPath<FMapProperty> Property)
		: Path(::MoveTemp(Path))
		, Property(::MoveTemp(Property))
	{
	}

	FVersePersistentVar() = default;

	UPROPERTY()
	FString Path;
	UPROPERTY()
	TFieldPath<FMapProperty> Property;
};

USTRUCT()
struct FVerseSessionVar
{
	GENERATED_BODY()

	explicit FVerseSessionVar(TFieldPath<FMapProperty> Property)
		: Property(::MoveTemp(Property))
	{
	}

	FVerseSessionVar() = default;

	UPROPERTY()
	TFieldPath<FMapProperty> Property;
};

USTRUCT()
struct FVerseClassVarAccessor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UFunction> Func{};

	UPROPERTY()
	bool bIsInstanceMember{false};

	UPROPERTY()
	bool bIsFallible{false};
};

USTRUCT()
struct FVerseClassVarAccessors
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<int, FVerseClassVarAccessor> Getters;

	UPROPERTY()
	TMap<int, FVerseClassVarAccessor> Setters;
};

struct FVerseFunctionDescriptor
{
	UObject* Owner = nullptr;
	UFunction* Function = nullptr; // May be nullptr even when valid
	FName DisplayName = NAME_None;
	FName UEName = NAME_None;

	FVerseFunctionDescriptor() = default;

	FVerseFunctionDescriptor(
		UObject* InOwner,
		UFunction* InFunction,
		FName InDisplayName,
		FName InUEName)
		: Owner(InOwner)
		, Function(InFunction)
		, DisplayName(InDisplayName)
		, UEName(InUEName)
	{
	}

	operator bool() const
	{
		return Owner != nullptr;
	}
};

// This class is deliberately simple (i.e. POD) to keep generated code size down.
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
struct FVerseCallableThunk
{
	const char* NameUTF8;
	Verse::VNativeFunction::FThunkFn Pointer;
};
#endif

UCLASS(MinimalAPI, within = Package, Config = Engine)
class UVerseClass : public UClass
{
	GENERATED_BODY()

public:
	UVerseClass() = default;
	explicit UVerseClass(
		EStaticConstructor,
		FName InName,
		uint32 InSize,
		uint32 InAlignment,
		EClassFlags InClassFlags,
		EClassCastFlags InClassCastFlags,
		const TCHAR* InClassConfigName,
		EObjectFlags InFlags,
		ClassConstructorType InClassConstructor,
		ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
		FUObjectCppClassStaticFunctions&& InCppClassStaticFunctions);
	explicit UVerseClass(const FObjectInitializer& ObjectInitializer);

#if UE_WITH_CONSTINIT_UOBJECT
	consteval UVerseClass(
		UE::CodeGen::ConstInit::FObjectParams ObjectParams,
		UE::CodeGen::ConstInit::FUFieldParams UFieldParams,
		UE::CodeGen::ConstInit::FStructParams StructParams,
		UE::CodeGen::ConstInit::FClassParams ClassParams,
		UE::CodeGen::ConstInit::FVerseClassParams InVerseParams)
		: Super(ObjectParams, UFieldParams, StructParams, ClassParams)
		, CompiledInMangledPackageVersePath(InVerseParams.MangledPackageVersePath)
		, SolClassFlags(VCLASS_UHTNative)
		, TaskClasses(ConstEval)
		, PersistentVars(ConstEval)
		, SessionVars(ConstEval)
		, VarAccessors(ConstEval)
		, CompiledInPackageRelativeVersePath(InVerseParams.PackageRelativeVersePath)
		, PackageRelativeVersePath(ConstEval)
		, DisplayNameToUENameFunctionMap(ConstEval)
		, DirectInterfaces(ConstEval)
		, PropertiesWrittenByInitCDO(ConstEval)
		, FunctionMangledNames(ConstEval)
		, PredictsFunctionNames(ConstEval)
		, PredictsVarNames(ConstEval)
		, PredictsCoercedFunctions(ConstEval)
		, IntPropertyConstraints(ConstEval)
		, DoublePropertyConstraints(ConstEval)
#if WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA
		, PreviousPathName(ConstEval)
#endif // WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA
#if WITH_VERSE_VM
		, VerseCallableThunks(InVerseParams.VerseCallableThunks)
#endif
	{
	}
#endif

public:
	//~ Begin UObjectBaseUtility interface
	COREUOBJECT_API virtual UE::Core::FVersePath GetVersePath() const override;
	//~ End UObjectBaseUtility interface

#if WITH_EDITORONLY_DATA
	UE_INTERNAL COREUOBJECT_API virtual void TrackDefaultInitializedProperties(void* DefaultData) const override;
#endif

private:
	//~ Begin UObject interface
	virtual bool IsAsset() const override;
	COREUOBJECT_API virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
	COREUOBJECT_API virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	COREUOBJECT_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	//~ End UObject interface

	//~ Begin UStruct interface
	COREUOBJECT_API virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	COREUOBJECT_API virtual void PreloadChildren(FArchive& Ar) override;
	COREUOBJECT_API virtual FString GetAuthoredNameForField(const FField* Field) const override;
	//~ End UStruct interface

	//~ Begin UClass interface
	COREUOBJECT_API virtual void PostInitInstance(UObject* InObj, FObjectInstancingGraph* InstanceGraph) override;
	COREUOBJECT_API virtual void PostLoadInstance(UObject* InObj) override;
	virtual bool CanCreateAssetOfClass() const override
	{
		return false;
	}
#if WITH_EDITORONLY_DATA
	COREUOBJECT_API virtual bool CanCreateInstanceDataObject() const override;
	COREUOBJECT_API virtual void SerializeDefaultObject(UObject* Object, FStructuredArchive::FSlot Slot) override;
#endif
#if WITH_EDITOR
	COREUOBJECT_API virtual FTopLevelAssetPath GetReinstancedClassPathName_Impl() const;
#endif
	//~ End UClass interface

	// UField interface.
	COREUOBJECT_API virtual const TCHAR* GetPrefixCPP() const override;
	// End of UField interface.

public:
#if UE_WITH_CONSTINIT_UOBJECT
	// CONSTINIT_UOBJECT_TODO: Size increase
	const UTF8CHAR* CompiledInMangledPackageVersePath = nullptr;
#endif // UE_WITH_CONSTINIT_UOBJECT

	UPROPERTY()
	uint32 SolClassFlags = VCLASS_None;

	// All coroutine task classes belonging to this class (one for each coroutine in this class)
	UPROPERTY()
	TArray<TObjectPtr<UVerseClass>> TaskClasses;

	/** Initialization function */
	UPROPERTY()
	TObjectPtr<UFunction> InitInstanceFunction;

	UPROPERTY()
	TArray<FVersePersistentVar> PersistentVars;

	UPROPERTY()
	TArray<FVerseSessionVar> SessionVars;

	UPROPERTY()
	TMap<FName, FVerseClassVarAccessors> VarAccessors;

	UPROPERTY()
	EVerseEffectSet ConstructorEffects = EVerseEffectSet::None;

	UPROPERTY()
	FName MangledPackageVersePath; // Storing as FName since it's shared between classes

#if UE_WITH_CONSTINIT_UOBJECT
	// CONSTINIT_UOBJECT_TODO: Size increase
	const UTF8CHAR* CompiledInPackageRelativeVersePath = nullptr;
#endif // UE_WITH_CONSTINIT_UOBJECT

	UPROPERTY()
	FString PackageRelativeVersePath;

	//~ This map is technically wrong since the FName is caseless...
	UPROPERTY()
	TMap<FName, FName> DisplayNameToUENameFunctionMap;

	// All interface class types that this class implements
	UPROPERTY()
	TArray<TObjectPtr<UVerseClass>> DirectInterfaces;

	UPROPERTY()
	TArray<TFieldPath<FProperty>> PropertiesWrittenByInitCDO;

	// Store a mapping from all previous function mangled names used by the
	// code generator to the current version of name mangling.  Store
	// NAME_None if there are multiple possible current versions for any
	// previous version.  If a previous function mangled name matches the
	// current mangled name, nothing is stored.
	UPROPERTY()
	TMap<FName, FName> FunctionMangledNames;

	UPROPERTY()
	TArray<FName> PredictsFunctionNames;

	UPROPERTY()
	TMap<FAnsiString, FName> PredictsVarNames;

	UPROPERTY()
	TMap<FName, FName> PredictsCoercedFunctions;

	UPROPERTY()
	TMap<TFieldPath<FInt64Property>, FVerseIntConstraints> IntPropertyConstraints;

	UPROPERTY()
	TMap<TFieldPath<FDoubleProperty>, FVerseDoubleConstraints> DoublePropertyConstraints;

#if WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA
	/** Path name this class had before it was marked as DEAD */
	FString PreviousPathName;
#endif // WITH_VERSE_COMPILER && WITH_EDITORONLY_DATA

	COREUOBJECT_API static const FName NativeParentClassTagName;
	COREUOBJECT_API static const FName PackageVersePathTagName;
	COREUOBJECT_API static const FName PackageRelativeVersePathTagName;

	// Name of the CDO init function
	COREUOBJECT_API static const FName InitCDOFunctionName;
	COREUOBJECT_API static const FName StructPaddingDummyName;

	// This is the asset path that all `UVerseClass` get when generated (we use it to identify assets as Verse classes)
	COREUOBJECT_API static const FTopLevelAssetPath VerseClassTopLevelAssetPath;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	UPROPERTY()
	Verse::TWriteBarrier<Verse::VClass> Class;

	// The VShape representing this class's layout. This is a placeholder before linking.
	Verse::VRestValue Shape{0};

	// A pointer to the CDO. This is a placeholder before its Verse constructor has run.
	Verse::VRestValue ConstructedDefaultObject{0};

	void SetShape(Verse::FAllocationContext Context, Verse::VShape* InShape);

	static Verse::FOpResult LoadField(Verse::FAllocationContext Context, UObject* Object, Verse::VUniqueString& FieldName, Verse::VNativeConstructorWrapper* Wrapper = nullptr);
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#endif

	/**
	 * Renames default sub-objects on a CDO so that they're unique (named after properties they are assigned to)
	 * @param  InObject Object (usually a CDO) whose default sub-objects are to be renamed
	 */
	COREUOBJECT_API static void RenameDefaultSubobjects(UObject* InObject);

	/**
	 * Checks that the sub-objects of a given Verse object are using the correct sub-archetype.
	 * @param  InObject Object whose default sub-objects we are validating
	 * @param  InArchetype The archetype of InObject
	 */
	COREUOBJECT_API static bool ValidateSubobjectArchetypes(UObject* InObject, UObject* InArchetype);

	void SetNeedsSubobjectInstancingForLoadedInstances(bool bNeedsInstancing)
	{
		bNeedsSubobjectInstancingForLoadedInstances = bNeedsInstancing;
	}

	// Allows dynamic instanced reference support to be toggled on/off for this class.
	COREUOBJECT_API void EnableDynamicInstancedReferenceSupport();
	COREUOBJECT_API void DisableDynamicInstancedReferenceSupport();

	bool IsNativeBound() const { return (SolClassFlags & VCLASS_NativeBound) != VCLASS_None; }
	bool IsUniversallyAccessible() const { return (SolClassFlags & VCLASS_UniversallyAccessible) != VCLASS_None; }
	bool IsConcrete() const { return (SolClassFlags & VCLASS_Concrete) != VCLASS_None; }
	bool IsVerseModule() const { return (SolClassFlags & VCLASS_Module) != VCLASS_None; }
	bool IsUHTNative() const { return (SolClassFlags & VCLASS_UHTNative) != VCLASS_None; }
	bool IsEpicInternal() const { return (SolClassFlags & VCLASS_EpicInternal) != VCLASS_None; }
	bool HasInstancedSemantics() const { return (SolClassFlags & VCLASS_HasInstancedSemantics) != VCLASS_None; }
	bool IsFinalSuper() const { return (SolClassFlags & VCLASS_FinalSuper) != VCLASS_None; }
	bool IsExplicitlyCastable() const { return (SolClassFlags & VCLASS_Castable) != VCLASS_None; }
	bool IsConstructorEpicInternal() const { return (SolClassFlags & VCLASS_EpicInternalConstructor) != VCLASS_None; }
	bool IsPersistable() const { return (SolClassFlags & VCLASS_Persistable) != VCLASS_None; }
	bool IsParametric() const { return (SolClassFlags & VCLASS_Parametric) != VCLASS_None; }
	bool IsVNIEpicInternal() const { return (SolClassFlags & VCLASS_VNIEpicInternal) != VCLASS_None; }
	bool IsErrIncomplete() const { return (SolClassFlags & VCLASS_Err_Incomplete) != VCLASS_None; }

	void SetNativeBound() { SolClassFlags |= VCLASS_NativeBound; }

	const FName* FindPredictsVarPropertyName(const FAnsiString& VarName)
	{
		const UVerseClass* VerseClass = this;
		while (VerseClass)
		{
			if (const FName* VarPropertyName = VerseClass->PredictsVarNames.Find(VarName))
			{
				return VarPropertyName;
			}

			VerseClass = Cast<UVerseClass>(VerseClass->GetSuperClass());
		}
		return nullptr;
	}

	const FVerseClassVarAccessors* FindAccessors(FName VarName) const
	{
		const UVerseClass* VerseClass = this;
		while (VerseClass)
		{
			if (const FVerseClassVarAccessors* Accessors = VerseClass->VarAccessors.Find(VarName))
			{
				return Accessors;
			}

			VerseClass = Cast<UVerseClass>(VerseClass->GetSuperClass());
		}
		return nullptr;
	}

	bool CanMemberFunctionBeCalledFromPredicts(FName FuncName)
	{
		const UVerseClass* VerseClass = this;
		while (VerseClass)
		{
			if (VerseClass->PredictsFunctionNames.Contains(FuncName))
			{
				return true;
			}

			VerseClass = Cast<UVerseClass>(VerseClass->GetSuperClass());
		}
		return false;
	}

	/**
	 * Iterates over Verse Function Properties on an object instance and executes a callback with VerseFunction value and its Verse name.
	 * @param Object Object instance to iterate Verse Functions for
	 * @param Operation callback for each of the found Verse Functions. When the callback returns false, iteration is stopped.
	 * @param IterationFlags Additional options used when iterating over Verse Function properties
	 */
	COREUOBJECT_API void ForEachVerseFunction(UObject* Object, TFunctionRef<bool(FVerseFunctionDescriptor)> Operation, EFieldIterationFlags IterationFlags = EFieldIterationFlags::None);

	FName GetFunctionMangledName(FName MangledName)
	{
		if (FName* NewMangledName = FindFunctionMangledName(MangledName))
		{
			return *NewMangledName;
		}
		return MangledName;
	}

	FName* FindFunctionMangledName(FName MangledName)
	{
		if (FName* NewMangledName = FindClassFunctionMangledName(MangledName))
		{
			return NewMangledName;
		}
		if (FName* NewMangledName = FindInterfaceFunctionMangledName(MangledName))
		{
			return NewMangledName;
		}
		return nullptr;
	}

	FName* FindInterfaceFunctionMangledName(FName MangledName)
	{
		for (const FImplementedInterface& Interface : Interfaces)
		{
			if (UVerseClass* SuperVerseClass = Cast<UVerseClass>(Interface.Class))
			{
				if (FName* NewMangledName = SuperVerseClass->FunctionMangledNames.Find(MangledName))
				{
					// @note there may not be two interface methods where one does not override
					// the other that share the same old mangled name, as the function name is
					// based on the base overridden definition.
					return NewMangledName;
				}
			}
		}
		return nullptr;
	}

	FName* FindClassFunctionMangledName(FName MangledName)
	{
		if (FName* NewMangledName = FunctionMangledNames.Find(MangledName))
		{
			return NewMangledName;
		}
		if (UVerseClass* SuperVerseClass = Cast<UVerseClass>(GetSuperClass()))
		{
			return SuperVerseClass->FindClassFunctionMangledName(MangledName);
		}
		return nullptr;
	}

	void AddFunctionMangledNames(FName OldMangledName, FName NewMangledName)
	{
		if (OldMangledName != NewMangledName)
		{
			if (FName* OtherNewMangledName = FindFunctionMangledName(OldMangledName))
			{
				if (*OtherNewMangledName != NewMangledName)
				{
					FunctionMangledNames.Add(OldMangledName, NAME_None);
				}
			}
			else
			{
				FunctionMangledNames.Add(OldMangledName, NewMangledName);
			}
		}
	}

	/**
	 * Returns a VerseFunction value given its display name
	 * @param Object Object instance to iterate Verse Functions for
	 * @param VerseName Display name of the function
	 * @param SearchFlags Additional options used when iterating over Verse Function properties
	 * @return VerseFunction value acquired from the provided Object instance or invalid function value if none was found.
	 */
#if WITH_VERSE_BPVM
	COREUOBJECT_API FVerseFunctionDescriptor FindVerseFunctionByDisplayName(UObject* Object, const FString& DisplayName, EFieldIterationFlags SearchFlags = EFieldIterationFlags::None);
#endif // WITH_VERSE_BPVM

	/**
	 * Returns the number of parameters a verse function takes
	 */
	COREUOBJECT_API static int32 GetVerseFunctionParameterCount(UFunction* Func);

	struct FStaleClassInfo
	{
		TObjectPtr<UVerseClass> SourceClass;
		TMap<FName, FName> DisplayNameToUENameFunctionMap;
		TMap<FName, FName> FunctionMangledNames;
		TArray<TObjectPtr<UVerseClass>> TaskClasses;
		TArray<TKeyValuePair<FName, TObjectPtr<UField>>> Children;
	};

	// Reset the contents of the UHT class and return the reset information so it can be restored if the compiled failed.
	// Being able to restore will probably not be needed once BPVM is removed
	COREUOBJECT_API FStaleClassInfo ResetUHTNative();

	// Strip verse generated functions from the function list and place into the output container for later restoring
	COREUOBJECT_API void StripVerseGeneratedFunctions(TArray<TKeyValuePair<FName, TObjectPtr<UField>>>* StrippedFields);

#if WITH_VERSE_BPVM
	COREUOBJECT_API void BindVerseFunction(const char* DecoratedFunctionName, FNativeFuncPtr NativeThunkPtr);
	COREUOBJECT_API void BindVerseCoroClass(const char* DecoratedFunctionName, FNativeFuncPtr NativeThunkPtr);
#endif

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	// Set a table of thunks for native functions callable from Verse. Parameter should be a static array as it will not be copied.
	COREUOBJECT_API void SetVerseCallableThunks(const FVerseCallableThunk* InThunks, uint32 NumThunks);
	COREUOBJECT_API void BindVerseCallableFunctions(Verse::VPackage* VersePackage, FUtf8StringView VerseScopePath);
#endif

	const FVniTypeDesc* GetNativeTypeDesc()
	{
		return NativeTypeDesc;
	}
	void SetNativeTypeDesc(const FVniTypeDesc* InNativeTypeDesc) { NativeTypeDesc = InNativeTypeDesc; }

private:
	COREUOBJECT_API void CallInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph);
	COREUOBJECT_API void CallPropertyInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph);
	COREUOBJECT_API void InstanceNewSubobjects(TNotNull<UObject*> InObj);

#if WITH_VERSE_BPVM
	COREUOBJECT_API void AddPersistentVars(UObject*);
#endif

	COREUOBJECT_API void AddSessionVars(UObject*);

	/** True if this class needs to run subobject instancing on loaded instances of classes (by default the engine does not run subobject instancing on instances that are being loaded) */
	bool bNeedsSubobjectInstancingForLoadedInstances = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UClassCookedMetaData> CachedCookedMetaDataPtr;
#endif // WITH_EDITORONLY_DATA

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	TConstArrayView<FVerseCallableThunk> VerseCallableThunks;
#endif

	const FVniTypeDesc* NativeTypeDesc{nullptr};
};
