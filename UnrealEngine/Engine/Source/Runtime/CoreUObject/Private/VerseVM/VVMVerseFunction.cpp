// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMVerseFunction.h"
#include "VerseVM/Inline/VVMEnterVMInline.h"

#if WITH_VERSE_BPVM
#include "VerseVM/VVMPackageName.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(VVMVerseFunction)

UVerseFunction::UVerseFunction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UVerseFunction::UVerseFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: Super(ObjectInitializer, InSuperFunction, InFunctionFlags, ParamsSize)
{
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void UVerseFunction::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UVerseFunction* This = static_cast<UVerseFunction*>(InThis);
	Collector.AddReferencedVerseValue(This->Callee);
}

namespace
{
DEFINE_FUNCTION(InvokeCalleeThunk)
{
	Verse::FOpResult OpResult{Verse::FOpResult::Error};

	AutoRTFM::Open([&] {
		// TODO: Marshal arguments
		Verse::VFunction::Args ArgValues;
		P_FINISH;

		Verse::FRunningContext RunningContext = Verse::FRunningContextPromise{};
		UVerseFunction* ThisFunction = CastChecked<UVerseFunction>(Stack.CurrentNativeFunction);
		Verse::VValue Self(Context);

		// Invoke the callee
		RunningContext.EnterVM([RunningContext, ThisFunction, Self, &ArgValues, &OpResult] {
			Verse::VValue Callee = ThisFunction->Callee.Get();
			if (Verse::VFunction* Function = Callee.DynamicCast<Verse::VFunction>())
			{
				OpResult = Function->InvokeWithSelf(RunningContext, Self, MoveTemp(ArgValues));
			}
			else if (Verse::VNativeFunction* NativeFunction = Callee.DynamicCast<Verse::VNativeFunction>())
			{
				OpResult = NativeFunction->Thunk(RunningContext, Self, ArgValues);
			}
		});
	});

	// TODO: Marshal return value and handle other outcomes
	ensure(OpResult.IsReturn());
}
} // namespace
#endif

void UVerseFunction::Bind()
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	SetNativeFunc(&InvokeCalleeThunk);
#endif
#if WITH_VERSE_BPVM
	if (EnumHasAnyFlags(FunctionFlags, FUNC_Native) && EnumHasAnyFlags(VerseFunctionFlags, EVerseFunctionFlags::UHTTaskUpdate))
	{
		UClass* OwnerClass = GetOwnerClass();
		if (OwnerClass != nullptr)
		{
			FName Name = GetFName();
			FNativeFunctionLookup* Found = OwnerClass->NativeFunctionLookupTable.FindByPredicate([=](const FNativeFunctionLookup& NativeFunctionLookup) { return Name == NativeFunctionLookup.Name; });
			if (Found)
			{
				SetNativeFunc(Found->Pointer);
				return;
			}
		}

		if (TryBindingCoroutine())
		{
			return;
		}
	}

	UFunction::Bind();
#endif
}

#if WITH_VERSE_BPVM
bool UVerseFunction::TryBindingCoroutine()
{
	UClass* TaskClass = GetOwnerClass();
	if (TaskClass == nullptr)
	{
		return false;
	}

	FNameBuilder TaskClassName(TaskClass->GetFName());
	TStringView TaskClassNameView(TaskClassName.ToView());
	if (!TaskClassNameView.StartsWith(Verse::FPackageName::TaskUClassPrefix))
	{
		return false;
	}

	TaskClassNameView.RightChopInline(FCString::Strlen(Verse::FPackageName::TaskUClassPrefix));
	int Index;
	if (!TaskClassNameView.FindChar('$', Index))
	{
		return false;
	}

	FStringView ClassNameView = TaskClassNameView.Left(Index);
	FStringView FuncNameView = TaskClassNameView.RightChop(Index + 1);
	UClass* VerseClass = FindObjectFast<UClass>(TaskClass->GetOuter(), FName(ClassNameView));
	if (VerseClass == nullptr)
	{
		return false;
	}

	FName Name(FuncNameView);
	FNativeFunctionLookup* Found = VerseClass->NativeFunctionLookupTable.FindByPredicate([=](const FNativeFunctionLookup& NativeFunctionLookup) { return Name == NativeFunctionLookup.Name; });
	if (Found)
	{
		SetNativeFunc(Found->Pointer);
		TaskClass->NativeFunctionLookupTable.Emplace(GetFName(), Found->Pointer);
		return true;
	}
	return false;
}

#endif

#if WITH_VERSE_VM
TOptional<FName> UVerseFunction::MaybeGetUFunctionFName(const Verse::VValue& Value)
{
	FUtf8StringView Name;
	if (Verse::VFunction* Func = Value.DynamicCast<Verse::VFunction>())
	{
		Name = Func->Procedure->Name->AsStringView();
	}
	else if (Verse::VNativeFunction* NativeFunc = Value.DynamicCast<Verse::VNativeFunction>())
	{
		Name = NativeFunc->Name->AsStringView();
	}
	else
	{
		return NullOpt;
	}
	return {Verse::Names::VerseFuncToUEFName(StringCast<TCHAR>(Name.GetData(), Name.Len()))};
}
#endif
