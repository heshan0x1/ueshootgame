// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Utf8String.h"
#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/ScriptMacros.h"
#include "VerseVM/VVMVerseClassFlags.h"
#include "VerseVM/VVMVerseConstraints.h"
#include "VerseVM/VVMVerseEffectSet.h"
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "UObject/GarbageCollection.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMWriteBarrier.h"
#endif

#include "VVMVerseStruct.generated.h"

class UStructCookedMetaData;
class UVerseClass;
struct FVniTypeDesc;
namespace Verse
{
struct VClass;
struct VShape;
} // namespace Verse

UCLASS(MinimalAPI)
class UVerseStruct : public UScriptStruct
{
	GENERATED_BODY()

	COREUOBJECT_API virtual void Serialize(FArchive& Ar) override;

	/** Creates the field/property links and gets structure ready for use at runtime */
	COREUOBJECT_API virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	COREUOBJECT_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	COREUOBJECT_API virtual uint32 GetStructTypeHash(const void* Src) const override;

public:
	UVerseStruct() = default;
	COREUOBJECT_API explicit UVerseStruct(
		const FObjectInitializer& ObjectInitializer,
		UScriptStruct* InSuperStruct,
		ICppStructOps* InCppStructOps = nullptr,
		EStructFlags InStructFlags = STRUCT_NoFlags,
		SIZE_T ExplicitSize = 0,
		SIZE_T ExplicitAlignment = 0);
	COREUOBJECT_API explicit UVerseStruct(const FObjectInitializer& ObjectInitializer);

#if UE_WITH_CONSTINIT_UOBJECT
	consteval UVerseStruct(
		UE::CodeGen::ConstInit::FObjectParams ObjectParams,
		UE::CodeGen::ConstInit::FUFieldParams UFieldParams,
		UE::CodeGen::ConstInit::FStructParams StructParams,
		UE::CodeGen::ConstInit::FScriptStructParams ScriptStructParams,
		UE::CodeGen::ConstInit::FVerseStructParams InVerseParams)
		: Super(ObjectParams, UFieldParams, StructParams, ScriptStructParams)
		, CompiledInQualifiedName(InVerseParams.QualifiedName)
		, VerseClassFlags(VCLASS_UHTNative)
		, QualifiedName(ConstEval)
		, Guid(InVerseParams.GuidA, InVerseParams.GuidB, InVerseParams.GuidC, InVerseParams.GuidD)
		, IntPropertyConstraints(ConstEval)
		, DoublePropertyConstraints(ConstEval)
	{
	}
#endif
#if UE_WITH_CONSTINIT_UOBJECT
	// CONSTINIT_UOBJECT_TODO: This increases size
	const UTF8CHAR* CompiledInQualifiedName = nullptr;
#endif // UE_WITH_CONSTINIT_UOBJECT

	/** EVerseClassFlags */
	UPROPERTY()
	uint32 VerseClassFlags = 0;

	UPROPERTY()
	FUtf8String QualifiedName;

	virtual FGuid GetCustomGuid() const override
	{
		return Guid;
	}

	/** Function used for initialization */
	UPROPERTY()
	TObjectPtr<UFunction> InitFunction;

	/** Parent module class */
	UPROPERTY()
	TObjectPtr<UVerseClass> ModuleClass;

	/** GUID to be able to match old version of this struct to new one */
	UPROPERTY()
	FGuid Guid;

	UPROPERTY()
	TObjectPtr<UFunction> FactoryFunction;

	UPROPERTY()
	TObjectPtr<UFunction> OverrideFactoryFunction;

	UPROPERTY()
	EVerseEffectSet ConstructorEffects = EVerseEffectSet::None;

	UPROPERTY()
	TMap<TFieldPath<FInt64Property>, FVerseIntConstraints> IntPropertyConstraints;

	UPROPERTY()
	TMap<TFieldPath<FDoubleProperty>, FVerseDoubleConstraints> DoublePropertyConstraints;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	UPROPERTY()
	Verse::TWriteBarrier<Verse::VClass> Class;

	Verse::VRestValue Shape{0};

	/** GC schema if needed, finalized in AssembleReferenceTokenStream */
	UE::GC::FSchemaOwner ReferenceSchema;

	COREUOBJECT_API void ResetUHTNative();
	void SetShape(Verse::FAllocationContext Context, Verse::VShape* InShape);

	static COREUOBJECT_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	COREUOBJECT_API void AssembleReferenceTokenStream(bool bForce = false);
#endif

	COREUOBJECT_API virtual FString GetAuthoredNameForField(const FField* Field) const override;

	COREUOBJECT_API void InvokeDefaultFactoryFunction(uint8* InStructData) const;

	bool IsNativeBound() const { return (VerseClassFlags & VCLASS_NativeBound) != VCLASS_None; }
	bool IsUHTNative() const { return (VerseClassFlags & VCLASS_UHTNative) != VCLASS_None; }
	bool IsTuple() const { return (VerseClassFlags & VCLASS_Tuple) != VCLASS_None; }
	bool IsParametric() const { return (VerseClassFlags & VCLASS_Parametric) != VCLASS_None; }

	void SetNativeBound() { VerseClassFlags |= VCLASS_NativeBound; }

	const FVniTypeDesc* GetNativeTypeDesc() { return NativeTypeDesc; }
	void SetNativeTypeDesc(const FVniTypeDesc* InNativeTypeDesc) { NativeTypeDesc = InNativeTypeDesc; }

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UStructCookedMetaData> CachedCookedMetaDataPtr;
#endif // WITH_EDITORONLY_DATA

	const FVniTypeDesc* NativeTypeDesc{nullptr};
};
