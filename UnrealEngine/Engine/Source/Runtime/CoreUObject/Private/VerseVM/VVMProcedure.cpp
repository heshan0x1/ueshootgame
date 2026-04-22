// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMBytecode.h"
#include "VerseVM/VVMBytecodeDispatcher.h"
#include "VerseVM/VVMBytecodeOps.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VProcedure);
TGlobalTrivialEmergentTypePtr<&VProcedure::StaticCppClassInfo> VProcedure::GlobalTrivialEmergentType;

template <typename TVisitor>
void VProcedure::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(FilePath, TEXT("FilePath"));
	Visitor.Visit(Name, TEXT("Name"));
	Visitor.Visit(GetNamedParamsBegin(), GetNamedParamsEnd(), TEXT("NamedParams"));
	Visitor.Visit(GetConstantsBegin(), GetConstantsEnd(), TEXT("Constants"));
	ForEachOpCode([&Visitor](auto& Op) {
		Op.ForEachOperand([&Visitor](EOperandRole Role, auto& Operand, const TCHAR* Name) {
			Visitor.Visit(Operand, Name);
		});
	});
	Visitor.Visit(GetRegisterNamesBegin(), GetRegisterNamesEnd(), TEXT("RegisterNames"));
}

void VProcedure::ConductCensusImpl()
{
	ForEachOpCode([](auto& Op) {
		using OpType = std::decay_t<decltype(Op)>;
		if constexpr (std::is_same_v<OpType, FOpLoadFieldICOffset>
					  || std::is_same_v<OpType, FOpLoadFieldICConstant>
					  || std::is_same_v<OpType, FOpLoadFieldICFunction>
					  || std::is_same_v<OpType, FOpLoadFieldICNativeFunction>
					  || std::is_same_v<OpType, FOpLoadFieldICAccessor>)
		{
			LoadLoadFence();
			VEmergentType* EmergentType = FHeap::EmergentTypeOffsetToPtr(Op.EmergentTypeOffset);
			// If we find ourselves here, the mutator cannot possibly get an IC hit. So we don't
			// need to worry about racing. The only thing that's important is the below opcode
			// store happens before we reuse the emergent type with a new allocation. Given that
			// we handshake after we run census and before we run destructors, this is guaranteed.
			if (!FHeap::IsMarked(EmergentType))
			{
				Op.Opcode = EOpcode::LoadField;
			}
		}
		else if constexpr (std::is_same_v<OpType, FOpNewObjectICClass>)
		{
			LoadLoadFence();
			VClass* CachedClass = Op.CachedClass;
			// If the VClass is not alive, the IC can never be hit.
			// We need to clear it before some other archetype happens to be allocated with the same pointer.
			if (!FHeap::IsMarked(CachedClass))
			{
				Op.Opcode = EOpcode::NewObject;
			}
		}
		else if constexpr (std::is_same_v<OpType, FOpCreateFieldICUObject>
						   || std::is_same_v<OpType, FOpCreateFieldICNativeStruct>
						   || std::is_same_v<OpType, FOpCreateFieldICValueObjectConstant>
						   || std::is_same_v<OpType, FOpCreateFieldICValueObjectField>)
		{
			LoadLoadFence();
			VEmergentType* SourceEmergentType = FHeap::EmergentTypeOffsetToPtr(Op.SourceEmergentTypeOffset);
			VEmergentType* NextEmergentType = FHeap::EmergentTypeOffsetToPtr(Op.NextEmergentTypeOffset);
			// If any one of the emergent types is dead, there's no way to do this field transition,
			// so we need to clear the IC before another emergent type is allocated with the same pointer.
			if (!(FHeap::IsMarked(SourceEmergentType) && FHeap::IsMarked(NextEmergentType)))
			{
				Op.Opcode = EOpcode::CreateField;
			}
		}
	});
}

namespace Private
{
// Helper type to detect if we need to serialize the given value
template <typename T>
struct OperandNeedsSerialization
{
};

template <>
struct OperandNeedsSerialization<FRegisterIndex> : public std::false_type
{
};

template <>
struct OperandNeedsSerialization<FValueOperand> : public std::false_type
{
};

template <typename T>
struct OperandNeedsSerialization<TOperandRange<T>> : public std::false_type
{
};

template <typename CellType>
struct OperandNeedsSerialization<TWriteBarrier<CellType>> : public std::true_type
{
};

// Disable VPackage serialization (in the NewClass opcode) for now.
template <>
struct OperandNeedsSerialization<TWriteBarrier<VPackage>> : public std::false_type
{
};
} // namespace Private

void VProcedure::SerializeLayout(FAllocationContext Context, VProcedure*& This, FStructuredArchiveVisitor& Visitor)
{
	uint32 NumNamedParameters = 0;
	uint32 NumConstants = 0;
	uint32 NumOpBytes = 0;
	uint32 NumOperands = 0;
	uint32 NumLabels = 0;
	uint32 NumUnwindEdges = 0;
	uint32 NumOpLocations = 0;
	uint32 NumRegisterNames = 0;
	if (!Visitor.IsLoading())
	{
		NumNamedParameters = This->NumNamedParameters;
		NumConstants = This->NumConstants;
		NumOpBytes = This->NumOpBytes;
		NumOperands = This->NumOperands;
		NumLabels = This->NumLabels;
		NumUnwindEdges = This->NumUnwindEdges;
		NumOpLocations = This->NumOpLocations;
		NumRegisterNames = This->NumRegisterNames;
	}

	Visitor.Visit(NumNamedParameters, TEXT("NumNamedParameters"));
	Visitor.Visit(NumConstants, TEXT("NumConstants"));
	Visitor.Visit(NumOpBytes, TEXT("NumOpBytes"));
	Visitor.Visit(NumOperands, TEXT("NumOperands"));
	Visitor.Visit(NumLabels, TEXT("NumLabels"));
	Visitor.Visit(NumUnwindEdges, TEXT("NumUnwindEdges"));
	Visitor.Visit(NumOpLocations, TEXT("NumOpLocations"));
	Visitor.Visit(NumRegisterNames, TEXT("NumRegisterNames"));
	if (Visitor.IsLoading())
	{
		This = &VProcedure::NewUninitialized(
			Context,
			NumNamedParameters,
			NumConstants,
			NumOpBytes,
			NumOperands,
			NumLabels,
			NumUnwindEdges,
			NumOpLocations,
			NumRegisterNames);
	}

	void* OpBytes = nullptr;
	TArray<uint8> SanitizedOpCodes;
	if (Visitor.IsLoading())
	{
		OpBytes = This->GetOpsBegin();
	}
	else
	{
		SanitizedOpCodes = This->SanitizeOpCodes();
		OpBytes = SanitizedOpCodes.GetData();
	}
	Visitor.VisitBulkData(OpBytes, NumOpBytes, TEXT("OpBytes"));
}

void VProcedure::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	Visitor.Visit(FilePath, TEXT("FilePath"));
	Visitor.Visit(Name, TEXT("Name"));
	Visitor.Visit(NumRegisters, TEXT("NumRegisters"));
	Visitor.Visit(NumPositionalParameters, TEXT("NumPositionalParameters"));
	Visitor.Visit(GetNamedParamsBegin(), GetNamedParamsEnd(), TEXT("NamedParameters"));
	Visitor.Visit(GetConstantsBegin(), GetConstantsEnd(), TEXT("Constants"));

	Visitor.VisitArray(TEXT("OperandValues"), [this, &Visitor] {
		ForEachOpCode([this, &Visitor](auto& Op) {
			Op.ForEachOperand([this, &Visitor](EOperandRole, auto& Operand, const TCHAR* OperandName) {
				using DecayedType = std::decay_t<decltype(Operand)>;
				if constexpr (Private::OperandNeedsSerialization<DecayedType>::value)
				{
					Visitor.Visit(Operand, OperandName);
				}
			});
		});
	});

	Visitor.Visit(GetOperandsBegin(), GetOperandsEnd(), TEXT("Operands"));
	Visitor.Visit(GetLabelsBegin(), GetLabelsEnd(), TEXT("Labels"));
	Visitor.Visit(GetUnwindEdgesBegin(), GetUnwindEdgesEnd(), TEXT("UnwindEdges"));
	Visitor.Visit(GetOpLocationsBegin(), GetOpLocationsEnd(), TEXT("OpLocations"));
	Visitor.Visit(GetRegisterNamesBegin(), GetRegisterNamesEnd(), TEXT("RegisterNames"));
}

TArray<uint8> VProcedure::SanitizeOpCodes()
{
	TArray<uint8> SanitizedOpCodes(BitCast<uint8*>(GetOpsBegin()), NumOpBytes);

	// Scan the opcodes looking for any operands that will need to be written out seperately.
	// Blank them out in the sanitized op codes to make the output more deterministic.
	int32 NumValues = 0;
	ForEachOpCode([this, &SanitizedOpCodes, &NumValues](auto& Op) {
		Op.ForEachOperand([this, &SanitizedOpCodes, &NumValues](EOperandRole, auto& Operand, const TCHAR*) {
			using DecayedType = std::decay_t<decltype(Operand)>;
			if constexpr (Private::OperandNeedsSerialization<DecayedType>::value)
			{
				NumValues++;
				uint32 ByteOffset = BytecodeOffset(&Operand);
				check(ByteOffset >= 0 && ByteOffset + sizeof(Operand) <= NumOpBytes);
				FMemory::Memset(&SanitizedOpCodes[ByteOffset], 0, sizeof(Operand));
			}
		});

		using OpType = std::decay_t<decltype(Op)>;
		OpType* SavedOp = BitCast<OpType*>(&SanitizedOpCodes[BytecodeOffset(&Op)]);
		if constexpr (std::is_same_v<OpType, FOpLoadFieldICOffset>
					  || std::is_same_v<OpType, FOpLoadFieldICConstant>
					  || std::is_same_v<OpType, FOpLoadFieldICFunction>
					  || std::is_same_v<OpType, FOpLoadFieldICNativeFunction>
					  || std::is_same_v<OpType, FOpLoadFieldICAccessor>)
		{
			SavedOp->Opcode = EOpcode::LoadField;
			// This isn't strictly needed, but it adds an extra bit of sanity.
			SavedOp->EmergentTypeOffset = 0;
			SavedOp->ICPayload = 0;
		}
		else if constexpr (std::is_same_v<OpType, FOpNewObjectICClass>)
		{
			SavedOp->Opcode = EOpcode::NewObject;
			// This isn't strictly needed, but it adds an extra bit of sanity.
			SavedOp->CachedClass = nullptr;
			SavedOp->EmergentTypeOffset = 0;
		}
		else if constexpr (std::is_same_v<OpType, FOpCreateFieldICUObject>
						   || std::is_same_v<OpType, FOpCreateFieldICNativeStruct>
						   || std::is_same_v<OpType, FOpCreateFieldICValueObjectConstant>
						   || std::is_same_v<OpType, FOpCreateFieldICValueObjectField>)
		{
			SavedOp->Opcode = EOpcode::CreateField;
			// This isn't strictly needed, but it adds an extra bit of sanity.
			SavedOp->SourceEmergentTypeOffset = 0;
			SavedOp->FieldIndex = 0;
			SavedOp->NextEmergentTypeOffset = 0;
		}
	});

	return SanitizedOpCodes;
}

template <typename FuncType>
void VProcedure::ForEachOpCode(FuncType&& Func)
{
	DispatchOps(*this, Forward<FuncType>(Func));
}

} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
