// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMBytecodeAnalysis.h"

#include "Algo/BinarySearch.h"

#include "Misc/ReverseIterate.h"

#include "VerseVM/Inline/VVMBytecodeInline.h"
#include "VerseVM/VVMBytecodeDispatcher.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMOverloaded.h"
#include "VerseVM/VVMProcedure.h"

namespace Verse
{
namespace BytecodeAnalysis
{
static TSet<uint32> ComputeJumpTargets(VProcedure& Procedure)
{
	TSet<uint32> Targets;

	DispatchOps(Procedure, [&](FOp& CurrentOp) {
		bool bHandled = true;

		EOpcode Opcode = CurrentOp.Opcode;

		auto AddOffset = [&](FLabelOffset& LabelOffset, const TCHAR* Name) {
			// Make sure this analysis and IsBranch stay in sync.
			V_DIE_UNLESS(IsBranch(Opcode) || Opcode == EOpcode::BeginFailureContext || Opcode == EOpcode::EndFailureContext || Opcode == EOpcode::BeginTask);
			Targets.Add(Procedure.BytecodeOffset(LabelOffset.GetLabeledPC()));
		};

		switch (Opcode)
		{
			case EOpcode::Jump:
				static_cast<FOpJump&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::JumpIfInitialized:
				static_cast<FOpJumpIfInitialized&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::Switch:
				static_cast<FOpSwitch&>(CurrentOp).ForEachJump([&](TOperandRange<FLabelOffset> LabelOffsets, const TCHAR* Name) {
					for (uint32 I = 0; I < LabelOffsets.Num; ++I)
					{
						AddOffset(Procedure.GetLabelsBegin()[LabelOffsets.Index + I], Name);
					}
				});
				break;
			case EOpcode::LtFastFail:
				static_cast<FOpLtFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::LteFastFail:
				static_cast<FOpLteFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::GtFastFail:
				static_cast<FOpGtFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::GteFastFail:
				static_cast<FOpGteFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::EqFastFail:
				static_cast<FOpEqFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::NeqFastFail:
				static_cast<FOpNeqFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::ArrayIndexFastFail:
				static_cast<FOpArrayIndexFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::TypeCastFastFail:
				static_cast<FOpTypeCastFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::QueryFastFail:
				static_cast<FOpQueryFastFail&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::CreateField:
				static_cast<FOpCreateField&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::CreateFieldICValueObjectConstant:
			case EOpcode::CreateFieldICValueObjectField:
			case EOpcode::CreateFieldICNativeStruct:
			case EOpcode::CreateFieldICUObject:
				static_cast<FOpCreateField&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::BeginFailureContext:
				// We treat the failure PC as a jump target.
				static_cast<FOpBeginFailureContext&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::EndFastFailureContext:
			case EOpcode::EndFailureContext:
				// The label for this opcode is just to branch around the then/else during leniency.
				// We don't model this.
				break;
			case EOpcode::BeginTask:
				// The yield PC is jumped to by something, even though we don't know what will jump there.
				static_cast<FOpBeginTask&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::Yield:
				static_cast<FOpYield&>(CurrentOp).ForEachJump(AddOffset);
				break;
			case EOpcode::BeginDefaultConstructor:
				static_cast<FOpBeginDefaultConstructor&>(CurrentOp).ForEachJump(AddOffset);
				break;
			default:
				bHandled = false;
				break;
		}

		if (!bHandled)
		{
			CurrentOp.ForEachJump(Procedure, [Opcode](auto&, const TCHAR*) {
				V_DIE("Jumps for opcode %hs should be handled above. Don't forget to also handle them below in `MakeBytecodeCFG`.", ToString(Opcode));
			});
		}
	});

	for (auto I = Procedure.GetUnwindEdgesBegin(), Last = Procedure.GetUnwindEdgesEnd(); I != Last; ++I)
	{
		Targets.Add(Procedure.BytecodeOffset(I->OnUnwind.GetLabeledPC()));
	}

	return Targets;
}

TUniquePtr<FCFG> MakeBytecodeCFG(VProcedure& Procedure)
{
	TSet<uint32> JumpTargets = ComputeJumpTargets(Procedure);
	TUniquePtr<FCFG> CFG(new FCFG{});
	TUniquePtr<FBasicBlock> CurrentBlock;
	bool bNextInstructionStartsNewBlock = true; // 0 is the entrypoint.
	DispatchOps(Procedure, [&](FOp& Op) {
		uint32 Offset = Procedure.BytecodeOffset(Op);
		if (bNextInstructionStartsNewBlock || JumpTargets.Contains(Offset))
		{
			if (CurrentBlock)
			{
				CFG->Blocks.Push(::MoveTemp(CurrentBlock));
			}
			CurrentBlock = MakeUnique<FBasicBlock>();
			bNextInstructionStartsNewBlock = false;
		}

		CurrentBlock->Bytecodes.Push(Offset);

		if (IsBranch(Op.Opcode) || IsTerminal(Op.Opcode))
		{
			bNextInstructionStartsNewBlock = true;
		}
	});

	CFG->Blocks.Push(::MoveTemp(CurrentBlock));

	for (FBlockIndex I = 0; I < CFG->NumBlocks(); ++I)
	{
		FBasicBlock* Block = CFG->Blocks[I].Get();
		Block->Index = I;
	}

	auto FindBlock = [&](FOp* Op) -> FBasicBlock& {
		return CFG->GetJumpTarget(Procedure.BytecodeOffset(Op));
	};

	// Compute successors
	for (FBlockIndex I = 0; I < CFG->NumBlocks(); ++I)
	{
		FBasicBlock* Block = CFG->Blocks[I].Get();

		auto AppendSuccessor = [&](FBasicBlock& Successor) {
			if (!Block->Successors.Contains(&Successor))
			{
				Block->Successors.Push(&Successor);
			}

			if (!Successor.Predecessors.Contains(Block))
			{
				Successor.Predecessors.Push(Block);
			}
		};

		FOp* LastOp = Procedure.GetPCForOffset(Block->Last());
		switch (LastOp->Opcode)
		{
			case EOpcode::Jump:
				AppendSuccessor(FindBlock(static_cast<FOpJump*>(LastOp)->JumpOffset.GetLabeledPC()));
				break;
			case EOpcode::JumpIfInitialized:
				AppendSuccessor(FindBlock(static_cast<FOpJumpIfInitialized*>(LastOp)->JumpOffset.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]); // Fall through to the next one.
				break;
			case EOpcode::LtFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpLtFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::LteFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpLteFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::GtFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpGtFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::GteFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpGteFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::EqFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpEqFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::NeqFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpNeqFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::ArrayIndexFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpArrayIndexFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::TypeCastFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpTypeCastFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::QueryFastFail:
				AppendSuccessor(FindBlock(static_cast<FOpQueryFastFail*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::CreateField:
			case EOpcode::CreateFieldICValueObjectConstant:
			case EOpcode::CreateFieldICValueObjectField:
			case EOpcode::CreateFieldICNativeStruct:
			case EOpcode::CreateFieldICUObject:
				AppendSuccessor(FindBlock(static_cast<FOpCreateField*>(LastOp)->OnFailure.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]);
				break;
			case EOpcode::Switch:
			{
				TOperandRange<FLabelOffset> LabelOffsets = static_cast<FOpSwitch*>(LastOp)->JumpOffsets;
				for (uint32 J = 0; J < LabelOffsets.Num; ++J)
				{
					AppendSuccessor(FindBlock(Procedure.GetLabelsBegin()[LabelOffsets.Index + J].GetLabeledPC()));
				}
				break;
			}
			case EOpcode::EndTask:
				break;
			case EOpcode::Yield:
				// This isn't strictly true.  The jump is really to the enclosing task
				// yield PC, then from some later PC to the `ResumeOffset`.
				AppendSuccessor(FindBlock(static_cast<FOpYield*>(LastOp)->ResumeOffset.GetLabeledPC()));
				break;
			case EOpcode::BeginDefaultConstructor:
				AppendSuccessor(FindBlock(static_cast<FOpBeginDefaultConstructor*>(LastOp)->OnDefaultSubObject.GetLabeledPC()));
				AppendSuccessor(*CFG->Blocks[I + 1]); // Fall through to the next one.
				break;
			default:
				if (MightFallThrough(LastOp->Opcode))
				{
					AppendSuccessor(*CFG->Blocks[I + 1]);
				}
				break;
		}
	}

	// Compute the mapping from bytecode offset to failure context. We require that the bytecode is constructed in such a way
	// so we can validate and produce the list of failure contexts in a single pass starting at the root block. The rule we validate
	// against is that every incoming edge to a basic block must contain the same failure context stack.
	{
		FBitArray VisitedBlocks;
		VisitedBlocks.Init(false, CFG->NumBlocks());

		TArray<TOptional<TArray<FFailureContext*>>> FailureContextsAtHead;
		FailureContextsAtHead.SetNum(CFG->NumBlocks());
		FailureContextsAtHead[0] = TArray<FFailureContext*>(); // The root starts without a failure context.

		TArray<FBasicBlock*> Worklist;
		Worklist.Push(CFG->Blocks[0].Get());

		auto GetOrCreateFailureContext = [&](FFailureContextId Id, FOp* FailurePC, FFailureContext* Parent) {
			if (Id.Id >= CFG->FailureContexts.Num())
			{
				CFG->FailureContexts.SetNum(Id.Id + 1);
			}
			TUniquePtr<FFailureContext>& FailureContext = CFG->FailureContexts[Id.Id];
			if (FailureContext)
			{
				V_DIE_UNLESS(FailureContext->Parent == Parent);
				V_DIE_UNLESS(FailureContext->FailurePC == FailurePC);
				return FailureContext.Get();
			}
			FailureContext = MakeUnique<FFailureContext>(Id, FailurePC, Parent);
			return FailureContext.Get();
		};

		auto MergeInto = [&](const TArray<FFailureContext*>& FailureContexts, FBasicBlock* Block) {
			Worklist.Push(Block);
			TOptional<TArray<FFailureContext*>>& IncomingFailureContexts = FailureContextsAtHead[Block->Index];
			if (!IncomingFailureContexts)
			{
				IncomingFailureContexts = FailureContexts;
			}
			else
			{
				V_DIE_UNLESS(IncomingFailureContexts.GetValue() == FailureContexts);
			}
		};

		while (Worklist.Num())
		{
			FBasicBlock* Block = Worklist.Pop();
			if (VisitedBlocks[Block->Index])
			{
				continue;
			}
			VisitedBlocks[Block->Index] = true;

			TOptional<TArray<FFailureContext*>>& OptionalFailureContexts = FailureContextsAtHead[Block->Index];
			V_DIE_UNLESS(OptionalFailureContexts);

			TArray<FFailureContext*> FailureContexts = OptionalFailureContexts.GetValue();

			for (uint32 InstOffset : Block->Bytecodes)
			{
				FOp* Op = Procedure.GetPCForOffset(InstOffset);

				auto AddInstToFailureContextMap = [&] {
					if (FailureContexts.Num())
					{
						CFG->BytecodeOffsetToFailureContext.Add(InstOffset, FailureContexts.Last());
					}
				};

				// BytecodeOffsetToFailureContext is exclusive of both BeginFailureContext and
				// EndFailureContext because neither of those opcodes can branch to FailurePC. Only things
				// inside the BeginFailureContext/EndFailureContext instruction range can.

				switch (Op->Opcode)
				{
					case EOpcode::BeginFailureContext:
					{
						AddInstToFailureContextMap();
						FOpBeginFailureContext* BeginOp = static_cast<FOpBeginFailureContext*>(Op);

						// We model the branches in this failure context to the "else" target. The failure context we enter
						// the BeginFailureContext opcode with is the same that it is at the "else".
						MergeInto(FailureContexts, &CFG->GetJumpTarget(Procedure.BytecodeOffset(BeginOp->OnFailure.GetLabeledPC())));

						FFailureContext* Parent = FailureContexts.Num() ? FailureContexts.Last() : nullptr;
						FailureContexts.Push(GetOrCreateFailureContext(BeginOp->Id, BeginOp->OnFailure.GetLabeledPC(), Parent));
						break;
					}
					case EOpcode::EndFailureContext:
					{
						FOpEndFailureContext* EndOp = static_cast<FOpEndFailureContext*>(Op);
						V_DIE_UNLESS(FailureContexts.Last()->Id == EndOp->Id);
						FailureContexts.Pop();

						AddInstToFailureContextMap();
						break;
					}
					case EOpcode::BeginTask:
					{
						AddInstToFailureContextMap();
						FOpBeginTask* BeginOp = static_cast<FOpBeginTask*>(Op);
						MergeInto(FailureContexts, &CFG->GetJumpTarget(Procedure.BytecodeOffset(BeginOp->OnYield.GetLabeledPC())));
						break;
					}
					default:
						AddInstToFailureContextMap();
						break;
				}
			}

			for (FBasicBlock* Successor : Block->Successors)
			{
				MergeInto(FailureContexts, Successor);
			}
		}
	}

	// Compute the mapping of bytecode offset to task. This is exclusive of BeginTask
	// but inclusive of EndTask because BeginTask can't branch to YieldPC but EndTask can.
	{
		TArray<FTask> Tasks;
		DispatchOps(Procedure, [&](FOp& Op) {
			if (Tasks.Num())
			{
				CFG->BytecodeOffsetToTask.Add(Procedure.BytecodeOffset(Op), Tasks.Last());
			}
			if (Op.Opcode == EOpcode::BeginTask)
			{
				FOpBeginTask& BeginOp = static_cast<FOpBeginTask&>(Op);
				Tasks.Push(FTask{BeginOp.OnYield.GetLabeledPC()});
			}
			if (Op.Opcode == EOpcode::EndTask)
			{
				Tasks.Pop();
			}
		});
	}

	CFG->FirstUnwindEdge = Procedure.GetUnwindEdgesBegin();
	CFG->LastUnwindEdge = Procedure.GetUnwindEdgesEnd();

	return CFG;
}

FBasicBlock& FCFG::GetJumpTarget(uint32 BytecodeOffset)
{
	uint32 Index = Algo::LowerBound(Blocks, BytecodeOffset, [&](const TUniquePtr<FBasicBlock>& Block, uint32 Offset) {
		return Block->Last() < Offset;
	});
	FBasicBlock* Result = Blocks[Index].Get();
	V_DIE_UNLESS(Result->First() == BytecodeOffset);
	return *Result;
}

FFailureContext* FCFG::FindCurrentFailureContext(uint32 BytecodeOffset)
{
	if (FFailureContext** Result = BytecodeOffsetToFailureContext.Find(BytecodeOffset))
	{
		return *Result;
	}
	return nullptr;
}

FTask* FCFG::FindCurrentTask(uint32 BytecodeOffset)
{
	return BytecodeOffsetToTask.Find(BytecodeOffset);
}

FOp* FCFG::FindCurrentUnwindPC(uint32 BytecodeOffset)
{
	auto NumUnwindEdges = static_cast<int32>(LastUnwindEdge - FirstUnwindEdge);
	auto IsBytecodeOffsetLessThanUnwindEdge = [](uint32 BytecodeOffset, const FUnwindEdge& Edge) {
		return BytecodeOffset <= Edge.Begin; // `FUnwindEdge` is exclusive on `Begin`.
	};
	auto IsUnwindEdgeLessThanBytecodeOffset = [](const FUnwindEdge& Edge, uint32 BytecodeOffset) {
		return Edge.End < BytecodeOffset; // `FUnwindEdge` is inclusive on `End`.
	};
	auto I = Algo::BinarySearch(
		TArrayView{FirstUnwindEdge, NumUnwindEdges},
		BytecodeOffset,
		FOverloaded{IsBytecodeOffsetLessThanUnwindEdge, IsUnwindEdgeLessThanBytecodeOffset});
	if (I == INDEX_NONE)
	{
		return nullptr;
	}
	return FirstUnwindEdge[I].OnUnwind.GetLabeledPC();
}

template <typename FunctionType>
static void ForEachDef(VProcedure& Procedure, FOp* Op, FunctionType&& Function)
{
	Op->ForEachReg(Procedure, [&](EOperandRole Role, FRegisterIndex Register) {
		if (IsAnyDef(Role))
		{
			Function(Register);
		}
	});
}

template <typename FunctionType>
static void ForEachUse(VProcedure& Procedure, FOp* Op, FunctionType&& Function)
{
	Op->ForEachReg(Procedure, [&](EOperandRole Role, FRegisterIndex Register) {
		if (IsAnyUse(Role))
		{
			Function(Register);
		}
	});
}

template <typename T>
concept COpCall = std::is_same_v<T, FOpCall> || std::is_same_v<T, FOpCallWithSelf>;

bool MaySuspend(FOp& Op)
{
	auto HandleOp = [](FOp&) {
		return false;
	};
	auto HandleOpYield = [](FOpYield& Op) {
		return true;
	};
	auto HandleOpCall = []<COpCall TOpCall>(TOpCall& Op) {
		return Op.bSuspends;
	};
	return DispatchOp(Op, FOverloaded{HandleOp, HandleOpYield, HandleOpCall});
}

// Note, the `FBasicBlock`s passed to `F` represent successors prior to
// `Op`, as opposed to `FBasicBlock::Successors`, which represent the succesors
// after evaluating all bytecode of the block.  The transfer function at `Op`
// should not yet be applied to the fact joined with the argument
// `FBasicBlock`'s fact for forward analyses, but should be applied beforehand
// for backward analyses.
template <typename FunctionType>
static void ForEachImplicitSuccessor(VProcedure& Procedure, FCFG& CFG, FOp& Op, FunctionType F)
{
	uint32 BytecodeOffset = Procedure.BytecodeOffset(Op);
	if (FFailureContext* FailureContext = CFG.FindCurrentFailureContext(BytecodeOffset))
	{
		uint32 FailureOffset = Procedure.BytecodeOffset(FailureContext->FailurePC);
		FBasicBlock& FailureBlock = CFG.GetJumpTarget(FailureOffset);
		Invoke(F, FailureBlock);
	}
	if (MaySuspend(Op))
	{
		if (FTask* Task = CFG.FindCurrentTask(BytecodeOffset))
		{
			uint32 YieldOffset = Procedure.BytecodeOffset(Task->YieldPC);
			FBasicBlock& YieldBlock = CFG.GetJumpTarget(YieldOffset);
			Invoke(F, YieldBlock);
		}
		if (FOp* UnwindPC = CFG.FindCurrentUnwindPC(BytecodeOffset))
		{
			uint32 UnwindOffset = Procedure.BytecodeOffset(UnwindPC);
			FBasicBlock& UnwindBlock = CFG.GetJumpTarget(UnwindOffset);
			Invoke(F, UnwindBlock);
		}
	}
}

FLiveness::FLocalCalc::FLocalCalc(FLiveness* Liveness, FBasicBlock* Block, VProcedure& Procedure)
	: Live(Liveness->LiveOut[Block->Index])
	, Liveness(Liveness)
	, Procedure(Procedure)
{
}

void FLiveness::FLocalCalc::Step(FOp* Op)
{
	ForEachDef(Procedure, Op, [&](FRegisterIndex Register) {
		Live[Register] = false;
	});
	ForEachUse(Procedure, Op, [&](FRegisterIndex Register) {
		Live[Register] = true;
	});

	// Everything live at the failure PC, yield PC, or unwind PC is live
	// throughout the body of the failure context or task body.
	ForEachImplicitSuccessor(Procedure, *Liveness->CFG, *Op, [&](FBasicBlock& Successor) {
		Live.Union(Liveness->LiveIn[Successor.Index]);
	});
}

TUniquePtr<FLiveness> ComputeBytecodeLiveness(FCFG& CFG, VProcedure& Procedure)
{
	TUniquePtr<FLiveness> Result = MakeUnique<FLiveness>(&CFG);

	Result->LiveOut.Init(FLiveSet(Procedure.NumRegisters), CFG.NumBlocks());
	Result->LiveIn.Init(FLiveSet(Procedure.NumRegisters), CFG.NumBlocks());

	bool bChanged;
	do
	{
		bChanged = false;
		for (FBlockIndex BlockIndex = CFG.NumBlocks(); BlockIndex--;)
		{
			FBasicBlock* Block = CFG.Blocks[BlockIndex].Get();
			FLiveness::FLocalCalc LocalCalc(Result.Get(), Block, Procedure);
			for (uint32 InstOffset : ReverseIterate(Block->Bytecodes))
			{
				FOp* Op = Procedure.GetPCForOffset(InstOffset);
				LocalCalc.Step(Op);
			}

			if (LocalCalc.Live != Result->LiveIn[BlockIndex])
			{
				bChanged = true;
				Result->LiveIn[BlockIndex] = LocalCalc.Live;
			}

			for (FBasicBlock* Predecessor : Block->Predecessors)
			{
				bChanged |= Result->LiveOut[Predecessor->Index].Union(LocalCalc.Live);
			}
		}
	}
	while (bChanged);

	return Result;
}

struct FInterferenceGraph
{
	TArray<TSet<FRegisterIndex>> InterferenceEdges;

	FInterferenceGraph(VProcedure& Procedure)
	{
		InterferenceEdges.SetNum(Procedure.NumRegisters);
	}

	void AddEdge(FRegisterIndex A, FRegisterIndex B)
	{
		if (A != B)
		{
			InterferenceEdges[A.Index].Add(B);
			InterferenceEdges[B.Index].Add(A);
		}
	}
};

TArray<FLiveRange> ComputeLiveRanges(VProcedure& Procedure, FLiveness& Liveness)
{
	FCFG* CFG = Liveness.CFG;

	TArray<FLiveRange> Result;
	Result.SetNum(Procedure.NumRegisters);

	for (FBlockIndex BlockIndex = 0; BlockIndex < CFG->NumBlocks(); ++BlockIndex)
	{
		FBasicBlock* Block = CFG->Blocks[BlockIndex].Get();
		FLiveness::FLocalCalc LocalCalc(&Liveness, Block, Procedure);
		for (uint32 InstOffset : ReverseIterate(Block->Bytecodes))
		{
			FOp* Op = Procedure.GetPCForOffset(InstOffset);
			LocalCalc.Step(Op);
			LocalCalc.Live.ForEach([&](FRegisterIndex LiveRegister) {
				Result[LiveRegister.Index].Join(InstOffset);
			});
		}
	}

	return Result;
}

struct FRegisterAllocator
{
	VProcedure& Procedure;
	const FCFG* CFG;
	TUniquePtr<FLiveness> Liveness;
	FInterferenceGraph InterferenceGraph;
	TArray<FRegisterIndex> RegisterAssignments;

	FRegisterAllocator(VProcedure& Procedure, FCFG& CFG)
		: Procedure(Procedure)
		, CFG(&CFG)
		, Liveness(ComputeBytecodeLiveness(CFG, Procedure))
		, InterferenceGraph(Procedure)
	{
	}

	// We allocate registers using a simple first fit allocator. We start by performing a liveness analysis and building
	// an interference graph between registers. Two registers interfere if they're live at the same time. If two registers
	// interfere, they can't be allocated to the same register. If they don't interfere, they can be allocated to the
	// same register.
	//
	// Once we have an interference graph, we walk each registers and assign it the lowest register that isn't used by
	// any of the registers it interferes with.
	void Allocate()
	{
		// TODO SOL-7793: Make this work with the debugger's register names.

		bool bUsesTasks = false;
		for (FBlockIndex BlockIndex = 0; BlockIndex < CFG->NumBlocks(); ++BlockIndex)
		{
			FBasicBlock* Block = CFG->Blocks[BlockIndex].Get();
			FLiveness::FLocalCalc LocalCalc(Liveness.Get(), Block, Procedure);
			for (uint32 InstOffset : ReverseIterate(Block->Bytecodes))
			{
				FOp* Op = Procedure.GetPCForOffset(InstOffset);

				if (Op->Opcode == EOpcode::BeginTask)
				{
					bUsesTasks = true;
				}

				ForEachDef(Procedure, Op, [&](FRegisterIndex Register) {
					LocalCalc.Live.ForEach([&](FRegisterIndex LiveRegister) {
						InterferenceGraph.AddEdge(Register, LiveRegister);
					});
				});

				LocalCalc.Step(Op);
			}
		}

		RegisterAssignments.SetNum(Procedure.NumRegisters);
		FRegisterIndex PinnedEnd = FRegisterIndex{FRegisterIndex::PARAMETER_START + Procedure.NumPositionalParameters + Procedure.NumNamedParameters};
		for (FRegisterIndex I = FRegisterIndex{0}; I < PinnedEnd; ++I)
		{
			RegisterAssignments[I.Index] = I;
		}

#if DO_GUARD_SLOW
		for (FRegisterIndex I = PinnedEnd; I < FRegisterIndex{Procedure.NumRegisters}; ++I)
		{
			V_DIE_IF(RegisterAssignments[I.Index]);
		}
#endif

		if (bUsesTasks)
		{
			TSet<FRegisterIndex> RegistersUsedInTasks;
			uint32 TaskCount = 0;
			DispatchOps(Procedure, [&](FOp& Op) {
				if (Op.Opcode == EOpcode::BeginTask)
				{
					++TaskCount;
				}
				if (TaskCount)
				{
					Op.ForEachReg(Procedure, [&](EOperandRole, FRegisterIndex Register) {
						if (Register >= PinnedEnd)
						{
							RegistersUsedInTasks.Add(Register);
						}
					});
				}
				if (Op.Opcode == EOpcode::EndTask)
				{
					V_DIE_UNLESS(TaskCount);
					--TaskCount;
				}
			});
			V_DIE_UNLESS(TaskCount == 0);

			FRegisterIndex NextToAssign = PinnedEnd;
			for (FRegisterIndex UsedInTask : RegistersUsedInTasks)
			{
				for (FRegisterIndex I = FRegisterIndex{0}; I < FRegisterIndex{Procedure.NumRegisters}; ++I)
				{
					InterferenceGraph.AddEdge(UsedInTask, I);
				}
				RegisterAssignments[UsedInTask.Index] = NextToAssign;
				++NextToAssign;
			}
		}

		for (FRegisterIndex ToAssign = PinnedEnd; ToAssign < FRegisterIndex{Procedure.NumRegisters}; ++ToAssign)
		{
			if (RegisterAssignments[ToAssign.Index])
			{
				continue;
			}
			TSet<FRegisterIndex> Disallowed;
			for (FRegisterIndex Interference : InterferenceGraph.InterferenceEdges[ToAssign.Index])
			{
				if (RegisterAssignments[Interference.Index])
				{
					Disallowed.Add(RegisterAssignments[Interference.Index]);
				}
			}

			for (FRegisterIndex J = PinnedEnd; true; ++J)
			{
				if (!Disallowed.Contains(J))
				{
					RegisterAssignments[ToAssign.Index] = J;
					break;
				}
			}
		}

		uint32 MaxRegister = 0;
		for (uint32 I = 0; I < Procedure.NumRegisters; ++I)
		{
			MaxRegister = std::max(RegisterAssignments[I].Index, MaxRegister);
		}

		if (false)
		{
			UE_LOG(LogVerseVM, Display, TEXT("OldSize: %u NewSize: %u"), Procedure.NumRegisters, MaxRegister + 1);
			if (true)
			{
				UE_LOG(LogVerseVM, Display, TEXT("Allocation:"));
				for (uint32 I = 0; I < Procedure.NumRegisters; ++I)
				{
					UE_LOG(LogVerseVM, Display, TEXT("\tr%u->r%u"), I, RegisterAssignments[I].Index);
				}
			}
		}

		// Calculate live ranges of the original register set before we rewrite the program.
		TArray<FLiveRange> LiveRanges = BytecodeAnalysis::ComputeLiveRanges(Procedure, *Liveness);

		Procedure.NumRegisters = MaxRegister + 1;

		for (FRegisterName* RegisterName = Procedure.GetRegisterNamesBegin(); RegisterName != Procedure.GetRegisterNamesEnd(); ++RegisterName)
		{
			RegisterName->LiveRange = RegisterName->Index < PinnedEnd ? FLiveRange::Top() : LiveRanges[RegisterName->Index.Index];
			RegisterName->Index = RegisterAssignments[RegisterName->Index.Index];
		}

		TArray<FLiveRange> AllocatedRegisterLiveRanges;
		AllocatedRegisterLiveRanges.SetNum(MaxRegister + 1);
		for (auto Start = RegisterAssignments.GetData(), Last = Start + RegisterAssignments.Num(), I = Start; I != Last; ++I)
		{
			AllocatedRegisterLiveRanges[I->Index].Join(LiveRanges[static_cast<int32>(I - Start)]);
		}

		auto HandleOp = [&](FOp& Op) {
			Op.ForEachReg(Procedure, [&](EOperandRole, FRegisterIndex& Register) {
				Register = RegisterAssignments[Register.Index];
			});
		};
		auto HandleOpReset = [&](FOpReset& Op) {
			HandleOp(Op);
			// Update the live range after the register index has been updated.
			// `Reset` rewrite requires live ranges accurate for the
			// actual register mutated, as opposed to the live range of the
			// register before register allocation.  See
			// `LoopingSuspendInsideTransaction` in await.versetest for why.
			Op.LiveRange = AllocatedRegisterLiveRanges[Op.Dest.Index];
		};
		DispatchOps(Procedure, FOverloaded{HandleOp, HandleOpReset});
	}
};

void AllocateRegisters(VProcedure& Procedure, FCFG& CFG)
{
	FRegisterAllocator Allocator{Procedure, CFG};
	Allocator.Allocate();
}

template <typename T>
static decltype(auto) Pop(TArray<T>& Buffer, FBitArray& Set)
{
	auto Value = Buffer.Pop();
	Set[Value] = false;
	return Value;
}

template <typename T, typename U>
static decltype(auto) Push(TArray<T>& Buffer, FBitArray& Set, U&& Value)
{
	if (Set[Value])
	{
		return;
	}
	Buffer.Emplace(::Forward<U>(Value));
	Set[Value] = true;
}

template <typename TFact>
concept CFact = requires(TFact& Left, const TFact& Right) {
	static_cast<bool>(Join(Left, Right));
};

template <typename TAnalyze, typename TFact>
concept CAnalyze = CFact<TFact> && requires(VProcedure& Procedure, FCFG& CFG, TAnalyze Analyze, FOp& Op, TFact& Fact) {
	Analyze(Procedure, CFG, Op, Fact);
};

template <typename TTransform, typename TFact>
concept CTransform = CFact<TFact> && requires(VProcedure& Procedure, FCFG& CFG, TTransform Transform, FOp& Op, const TFact& Fact) {
	static_cast<bool>(Transform(Procedure, CFG, Op, Fact));
};

template <CFact TFact, CAnalyze<TFact> TAnalyze, CTransform<TFact> TTransform>
static void ForwardAnalyzeAndTransform(
	VProcedure& Procedure,
	FCFG& CFG,
	TAnalyze Analyze,
	TTransform Transform)
{
	auto NumBlocks = CFG.NumBlocks();
	TArray<TFact> Facts;
	Facts.SetNum(NumBlocks);
	struct FTransformedOp
	{
		uint32 BytecodeOffset;
		TArray<std::byte> PrevOp;
	};
	TArray<TArray<FTransformedOp>> TransformedOps;
	TransformedOps.SetNum(NumBlocks);
	TArray<decltype(NumBlocks)> Buffer;
	Buffer.Reserve(NumBlocks);
	FBitArray Set{false, static_cast<int32>(NumBlocks)};
	for (decltype(NumBlocks) I = NumBlocks, Last = 0; I != Last; --I)
	{
		Push(Buffer, Set, I - 1);
	}
	while (!Buffer.IsEmpty())
	{
		uint32 I = Pop(Buffer, Set);
		FBasicBlock& Block = *CFG.Blocks[I];
		TFact BlockFact = Facts[I];
		TArray<FTransformedOp>& BlockTransformedOps = TransformedOps[I];
		// Revert transforms performed using a fact too low on the lattice.
		for (auto [BytecodeOffset, PrevOp] : BlockTransformedOps)
		{
			std::memcpy(Procedure.GetPCForOffset(BytecodeOffset), PrevOp.GetData(), PrevOp.Num());
		}
		BlockTransformedOps.Empty();
		for (uint32 BytecodeOffset : Block.Bytecodes)
		{
			FOp* Op = Procedure.GetPCForOffset(BytecodeOffset);
			ForEachImplicitSuccessor(Procedure, CFG, *Op, [&](FBasicBlock& Successor) {
				if (Join(Facts[Successor.Index], static_cast<const TFact&>(BlockFact)))
				{
					Push(Buffer, Set, Successor.Index);
				}
			});
			DispatchOp(*Op, [&](auto&& Op) {
				auto PrevOp = Op;
				if (Transform(Procedure, CFG, Op, static_cast<const TFact&>(BlockFact)))
				{
					// Store the untransformed `FOp` in the event the incoming
					// fact is moved up the lattice and the transform needs to
					// be reverted and reapplied using the now-higher fact.
					BlockTransformedOps.Emplace(BytecodeOffset, TArray{BitCast<std::byte*>(&PrevOp), sizeof PrevOp});
				}
			});
			// Analyze the (possibly transformed) `Op`.
			Analyze(Procedure, CFG, *Op, BlockFact);
		}
		for (FBasicBlock* Successor : Block.Successors)
		{
			if (Join(Facts[Successor->Index], static_cast<const TFact&>(BlockFact)))
			{
				Push(Buffer, Set, Successor->Index);
			}
		}
	}
}

// Resumptions, i.e. the instruction just after an instruction that suspends
// that _may_ be evaluated before the current instruction.
namespace ReachingResumptions
{
struct FFact
{
	TSet<uint32> BytecodeOffsets;
};

template <typename T>
static bool Union(TSet<T>& Left, const TSet<T>& Right)
{
	auto Num = Left.Num();
	Left.Append(Right);
	return Num != Left.Num();
}

static bool Join(FFact& Left, const FFact& Right)
{
	return Union(Left.BytecodeOffsets, Right.BytecodeOffsets);
}

static bool Analyze(VProcedure& Procedure, FCFG&, FOp& Op, FFact& Fact)
{
	auto HandleOp = [](FOp&) {
		return false;
	};
	auto HandleOpYield = [&](FOpYield& Op) {
		Fact.BytecodeOffsets.Empty();
		Fact.BytecodeOffsets.Add(Procedure.BytecodeOffset(Op.ResumeOffset.GetLabeledPC()));
		return true;
	};
	auto HandleOpCall = [&]<COpCall TOpCall>(TOpCall& Op) {
		if (!Op.bSuspends)
		{
			return false;
		}
		Fact.BytecodeOffsets.Add(Procedure.BytecodeOffset(&Op + 1));
		return true;
	};
	return DispatchOp(Op, FOverloaded{HandleOp, HandleOpYield, HandleOpCall});
}
} // namespace ReachingResumptions

// Destination of `Reset` instructions that _must_ be evaluated before
// the current instruction.
namespace DominatingResets
{
// Registers of dominating `Reset`s, partitioned by failure context.
struct FFact
{
	ReachingResumptions::FFact ReachingResumptions;

	// Indexed by failure context `Id`.  The `FBitArray` represents registers
	// from dominating `Reset`s.
	mutable TArray<FBitArray> Registers;

	TArray<FBitArray>& GetRegisters(int32 Num)
	{
		if (Num > Registers.Num())
		{
			Registers.SetNum(Num);
		}
		return Registers;
	}

	int32 GetRegistersIndex(FFailureContextId Id) const
	{
		auto Index = Id.Id + 1;
		if (Index >= Registers.Num())
		{
			Registers.SetNum(Index + 1);
		}
		return Index;
	}

	int32 GetRegistersIndex(const FFailureContext* FailureContext) const
	{
		uint32 Index;
		if (FailureContext)
		{
			Index = FailureContext->Id.Id + 1;
		}
		else
		{
			Index = 0;
		}
		if (Index >= Registers.Num())
		{
			Registers.SetNum(Index + 1);
		}
		return Index;
	}
};

static bool Intersection(FBitArray& Left, const FBitArray& Right)
{
	bool bChanged = false;
	// See BitwiseOperatorImpl.  The passed function's parameters are flipped
	// compared to BitwiseOperatorImpl's parameters.
	UE::Core::Private::BitwiseOperatorImpl(Right, Left, EBitwiseOperatorFlags::MaxSize, [&](uint32 Left, uint32 Right) {
		auto Result = Left & Right;
		if (Result != Left)
		{
			bChanged = true;
		}
		return Result;
	});
	return bChanged;
}

static void Add(FBitArray& This, int32 Index)
{
	if (Index >= This.Num())
	{
		This.SetNum(Index + 1, false);
	}
	This[Index] = true;
}

static bool Contains(const FBitArray& This, int32 Index)
{
	return Index < This.Num() && This[Index];
}

static bool Join(FFact& Left, const FFact& Right)
{
	bool bChanged = false;
	bChanged |= Join(Left.ReachingResumptions, Right.ReachingResumptions);
	const auto& RightRegisters = Right.Registers;
	auto Last = RightRegisters.Num();
	auto& LeftRegisters = Left.GetRegisters(Last);
	for (decltype(Last) I = 0; I != Last; ++I)
	{
		bChanged |= Intersection(LeftRegisters[I], RightRegisters[I]);
	}
	return bChanged;
}

static void Analyze(VProcedure& Procedure, FCFG& CFG, FOp& Op, FFact& Fact)
{
	int32 FailureContextIndex = Fact.GetRegistersIndex(CFG.FindCurrentFailureContext(Procedure.BytecodeOffset(Op)));
	if (Analyze(Procedure, CFG, Op, Fact.ReachingResumptions))
	{
		// The following instruction represents a resumption point.
		Fact.Registers[FailureContextIndex].Empty();
	}
	auto HandleOp = [](FOp&) {
	};
	// Add `Op.Dest` to this failure context's dominance information.
	auto HandleOpReset = [&](FOpReset& Op) {
		Add(Fact.Registers[FailureContextIndex], Op.Dest.Index);
	};
	// Commit out the dominance information from the ending failure context to
	// the enclosing failure context.
	auto HandleOpEndFailureContext = [&](FOpEndFailureContext& Op) {
		int32 EndingFailureContextIndex = Fact.GetRegistersIndex(Op.Id);
		Fact.Registers[FailureContextIndex] = Fact.Registers[EndingFailureContextIndex];
	};
	DispatchOp(Op, FOverloaded{HandleOp, HandleOpReset, HandleOpEndFailureContext});
}

// This transform assumes trailing need not occur before the mutation incurred
// by a `Reset` if either of the following hold:
// * a `Reset` dominates the current `Reset` instruction (with
//   care taken to account for the possibility of reevaluating at a resumption
//   point due to resuming in a failure context)
// * No reaching resumption point falls within the live range of the reset
//   register.  Put another way, the mutation due to this `ResetNonTrailed` may not be
//   observed to have not backtracked (if the resumption occurs in a failure
//   context and the failure context fails followed by resuming once more) by a
//   read.
// See `LoopingSuspendInsideTransaction` in await.versetest for an example
// involving both criteria.
static bool Transform(VProcedure& Procedure, FCFG& CFG, FOp& Op, const FFact& Fact)
{
	auto HandleOp = [](FOp& Op) {
		return false;
	};
	auto HandleOpReset = [&](FOpReset& Op) {
		FFailureContext* FailureContext = CFG.FindCurrentFailureContext(Procedure.BytecodeOffset(Op));
		int32 FailureContextIndex = Fact.GetRegistersIndex(FailureContext);
		if (!Contains(Fact.Registers[FailureContextIndex], Op.Dest.Index))
		{
			for (uint32 BytecodeOffset : Fact.ReachingResumptions.BytecodeOffsets)
			{
				if (Op.LiveRange.Contains(BytecodeOffset))
				{
					return false;
				}
			}
		}
		Op.Opcode = EOpcode::ResetNonTrailed;
		return true;
	};
	return DispatchOp(Op, FOverloaded{HandleOp, HandleOpReset});
}
} // namespace DominatingResets

void AnalyzeAndTransformResets(VProcedure& Procedure, FCFG& CFG)
{
	ForwardAnalyzeAndTransform<DominatingResets::FFact>(
		Procedure,
		CFG,
		DominatingResets::Analyze,
		DominatingResets::Transform);
}
} // namespace BytecodeAnalysis
} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
