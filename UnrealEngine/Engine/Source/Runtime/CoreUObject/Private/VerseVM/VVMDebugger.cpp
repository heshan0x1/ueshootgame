// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMDebugger.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMFrame.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMNativeFunction.h"

static Verse::FDebugger* GDebugger = nullptr;

Verse::FDebugger* Verse::GetDebugger()
{
	return GDebugger;
}

void Verse::SetDebugger(FDebugger* Arg)
{
	StoreStoreFence();
	GDebugger = Arg;
	if (Arg)
	{
		FContext::AttachedDebugger();
	}
	else
	{
		FContext::DetachedDebugger();
	}
}

namespace Verse
{
COREUOBJECT_API extern FOpErr StopInterpreterSentry;

namespace
{
bool IsFalse(VValue Arg)
{
	return Arg.IsCell() && &Arg.AsCell() == GlobalFalsePtr.Get();
}
} // namespace

void Debugger::ForEachStackFrame(
	FRunningContext Context,
	const FOp& PC,
	VFrame& Frame,
	VTask& Task,
	const FNativeFrame* NativeFrame,
	TFunctionRef<void(const FLocation*, FFrame)> F)
{
	TWriteBarrier<VUniqueString> SelfName{Context, VUniqueString::New(Context, "Self")};
	auto Loop = [&](const FOp& PC, VFrame& TopFrame) {
		const FOp* CurrentOp = &PC;
		for (VFrame* Frame = &TopFrame; Frame; Frame = Frame->CallerFrame.Get())
		{
			VUniqueString& FilePath = *Frame->Procedure->FilePath;
			if (FilePath.Num() == 0)
			{
				continue;
			}
			TArray<TTuple<TWriteBarrier<VUniqueString>, VValue>> Registers;
			VValue SelfValue = Frame->Registers[FRegisterIndex::SELF].Get(Context);
			V_DIE_IF_MSG(
				SelfValue.IsUninitialized(),
				"`Self` should have been bound by now for methods, and set to `GlobalFalse()` for functions. "
				"This indicates either a codegen issue, or a failure in `CallWithSelf`!");
			if (IsFalse(SelfValue))
			{
				Registers.Reserve(Frame->Procedure->NumRegisterNames);
			}
			else
			{
				Registers.Reserve(Frame->Procedure->NumRegisterNames + 1);
				Registers.Emplace(SelfName, Frame->Registers[FRegisterIndex::SELF].Get(Context));
			}

			uint32 BytecodeOffset = Frame->Procedure->BytecodeOffset(*CurrentOp);
			for (FRegisterName *I = Frame->Procedure->GetRegisterNamesBegin(), *Last = Frame->Procedure->GetRegisterNamesEnd(); I != Last; ++I)
			{
				VValue RegisterValue;
				if (I->LiveRange.Contains(BytecodeOffset))
				{
					RegisterValue = Frame->Registers[I->Index.Index].Get(Context);
				}
				Registers.Emplace(I->Name, RegisterValue);
			}

			FFrame DebuggerFrame{Context, *Frame->Procedure->Name, FilePath, ::MoveTemp(Registers)};
			if (CurrentOp == &StopInterpreterSentry)
			{
				F(nullptr, ::MoveTemp(DebuggerFrame));
			}
			else
			{
				const FLocation* Location = Frame->Procedure->GetLocation(*CurrentOp);
				F(Location, ::MoveTemp(DebuggerFrame));
			}
			CurrentOp = Frame->CallerPC;
		}
	};
	Loop(PC, Frame);
	NativeFrame->WalkTaskFrames(&Task, [&Context, &Loop, &F](const FNativeFrame& Frame) {
		if (const VNativeFunction* Callee = Frame.Callee)
		{
			FFrame DebuggerFrame{Context, *Callee->Name};
			F(nullptr, ::MoveTemp(DebuggerFrame));
		}
		Loop(*Frame.CallerPC, *Frame.CallerFrame);
	});
}
} // namespace Verse

#endif
