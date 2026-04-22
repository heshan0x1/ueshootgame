// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMWriteBarrier.h"

#include "VVMVerseFunction.generated.h"

UENUM(Flags)
enum class EVerseFunctionFlags : uint32
{
	None = 0x00000000u,
	UHTNative = 0x00000001u,
	UHTTaskUpdate = 0x00000002u,
};
ENUM_CLASS_FLAGS(EVerseFunctionFlags)

// A UFunction wrapper for a VerseVM callee (VFunction or VNativeFunction)
UCLASS(MinimalAPI)
class UVerseFunction : public UFunction
{
	GENERATED_BODY()

public:
	COREUOBJECT_API explicit UVerseFunction(const FObjectInitializer& ObjectInitializer);
	COREUOBJECT_API explicit UVerseFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);

#if UE_WITH_CONSTINIT_UOBJECT
	consteval UVerseFunction(
		UE::CodeGen::ConstInit::FObjectParams ObjectParams,
		UE::CodeGen::ConstInit::FUFieldParams UFieldParams,
		UE::CodeGen::ConstInit::FStructParams StructParams,
		UE::CodeGen::ConstInit::FFunctionParams FunctionParams,
		UE::CodeGen::ConstInit::FVerseFunctionParams InParams)
		: Super(ObjectParams, UFieldParams, StructParams, FunctionParams)
		, CompiledInAlternateNameUTF8(InParams.AlternateNameUTF8)
		, VerseFunctionFlags(EVerseFunctionFlags::UHTNative)
	{
	}

#endif

	union
	{
#if UE_WITH_CONSTINIT_UOBJECT
		// CONSTINIT_UOBJECT_TODO: Size increase
		const UTF8CHAR* CompiledInAlternateNameUTF8;
#endif // UE_WITH_CONSTINIT_UOBJECT
	   // The alternate name is used between the native function declaration and the expected verse name
		FName AlternateName = {};
	};

	EVerseFunctionFlags VerseFunctionFlags = EVerseFunctionFlags::None;

	bool IsUHTNative()
	{
		return EnumHasAnyFlags(VerseFunctionFlags, EVerseFunctionFlags::UHTNative);
	}

	static bool IsVerseGeneratedFunction(UField* Field)
	{
		if (UVerseFunction* VerseFunction = Cast<UVerseFunction>(Field))
		{
			return !VerseFunction->IsUHTNative();
		}
		return false;
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	COREUOBJECT_API static TOptional<FName> MaybeGetUFunctionFName(const Verse::VValue&);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	Verse::TWriteBarrier<Verse::VValue> Callee;
#endif

	virtual void Bind() override;

private:
#if WITH_VERSE_BPVM
	bool TryBindingCoroutine();
#endif
};
