// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationOperations.cpp —— Iris 状态级 / 对象级 / 协议级操作的实现
// ---------------------------------------------------------------------------------------------------------------------
// 本文件实现 ReplicationOperations.h 中三套门面：
//   - FReplicationStateOperations    —— 单 Descriptor 粒度（Quantize / Dequantize / Serialize / IsEqual / Validate / Apply）。
//   - FReplicationInstanceOperations —— Instance 粒度（PollAndCopy / Quantize 整对象 / Reset / DequantizeAndApply）。
//   - FReplicationProtocolOperations —— Protocol 粒度（带 ChangeMask 序列化 / 与默认或基线的 delta）。
//
// 主要技术点：
//   1) 所有操作以"逐成员循环"为模式，使用 Descriptor 提供的 MemberDescriptors / MemberSerializerDescriptors / 
//      MemberChangeMaskDescriptors 数组，按 Internal/External 偏移把数据搬来搬去。
//   2) Quantize/Dequantize 走 NetSerializer 的虚函数，保证类型相关的"打包逻辑"（精度量化、压缩格式）在 Serializer 自治。
//   3) ChangeMask：每个 ReplicationState 有一段位图（External buffer 中预留），按位指示每个 ChangeMask 段是否脏。
//      位偏移与位宽由 MemberChangeMaskDescriptor 给出（一个成员可能占多 bit，例如数组元素 modulo 散列）。
//   4) 文件末尾的 AppendMemberChangeMasks / ResetMemberChangeMasks 是把状态级 ChangeMask 拼接到对象级 BitStream 的核心。
//   5) Conditional ChangeMask：当 Protocol 含 HasConditionalChangeMask（lifetime conditional）时还要拼一份 conditional mask。
//   6) Init State 特殊处理：init state 不属于 ChangeMask 跟踪，且仅在 Context.IsInitState() 时序列化。
//
// CVar 行为：
//   - net.Iris.DeltaCompressInitialState（默认 true）：初始状态相对默认状态 delta，节省带宽。
//   - net.Iris.OnlyQuantizeDirtyMembers（默认 true）：QuantizeIfDirty 仅 quantize 标脏成员。
// =====================================================================================================================

#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationState/InternalPropertyReplicationState.h"
#include "Iris/ReplicationState/InternalReplicationStateDescriptorUtils.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Misc/CoreMiscDefines.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "DequantizeAndApplyHelper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/UnrealMemory.h"
#include "UObject/CoreNetTypes.h"

#include "CoreTypes.h"


namespace UE::Net::Private
{

// 用于 NetTrace 输出 lifetime condition 名（COND_None / COND_InitialOnly / ...）。
// 编号与 ELifetimeCondition 的整数值一一对应；Reset 函数（文件末尾）可清空 DebugNameId。
static FNetDebugName s_LifetimeConditionDebugNames[] = 
{
	{TEXT("None"), 0},
	{TEXT("InitialOnly"), 0},
	{TEXT("OwnerOnly"), 0},
	{TEXT("SkipOwner"), 0},
	{TEXT("SimulatedOnly"), 0},
	{TEXT("AutonomousOnly"), 0},
	{TEXT("SimulatedOrPhysics"), 0},
	{TEXT("InitialOrOwner"), 0},
	{TEXT("Custom"), 0},
	{TEXT("ReplayOrOwner"), 0},
	{TEXT("ReplayOnly"), 0},
	{TEXT("SimulatedOnlyNoReplay"), 0},
	{TEXT("SimulatedOrPhysicsNoReplay"), 0},
	{TEXT("SkipReplay"), 0},
	{TEXT("Dynamic"), 0},
	{TEXT("Never"), 0},
	{TEXT("NetGroup"), 0},
};
static_assert(COND_Max == 17, "s_LifetimeConditionDebugNames may need updating.");

// Append changemask bits to ChangeMaskWriter and conditionally to the ConditionalChangeMaskWriter. If the state is dirty, the function will return true.
// 把单个 ReplicationState 的 ChangeMask（位图）拼接到对象级 ChangeMask BitStream，并返回该 state 是否脏。
// 同时也处理 ConditionalChangeMask：仅当 Descriptor 标记 HasLifetimeConditionals 时才有 conditional 段需要写。
static bool AppendMemberChangeMasks(FNetBitStreamWriter* ChangeMaskWriter, FNetBitStreamWriter* ConditionalChangeMaskWriter, uint8* ExternalStateBuffer, const FReplicationStateDescriptor* Descriptor);
// 提交后清空 ChangeMask 并把 state header 的 dirty 位重置（成员被 ack 给 Writer 后调用）。
static void ResetMemberChangeMasks(uint8* ExternalStateBuffer, const FReplicationStateDescriptor* Descriptor);
// 调试断言：确保 Context 持有的 ChangeMask 与传入的 ChangeMask 是同一份位图（避免 stack 上的临时位图遗留）。
static void CheckChangeMask(const FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask) { check(Context.GetChangeMask() && (Context.GetChangeMask()->GetData() == ChangeMask.GetData()));}

}

namespace UE::Net
{

static bool bDeltaCompressInitialState = true;
static FAutoConsoleVariableRef CVarbDeltaCompressInitialState(
	TEXT("net.Iris.DeltaCompressInitialState"),
	bDeltaCompressInitialState,
	TEXT("if true we compare with default state when serializing inital state."
	));

static bool bOnlyQuantizeDirtyMembers = true;
static FAutoConsoleVariableRef CVarbOnlyQuantizeDirtyMembers(
	TEXT("net.Iris.OnlyQuantizeDirtyMembers"),
	bOnlyQuantizeDirtyMembers,
	TEXT("if true we only quantize members marked as dirty unless this is a new object."
	));

void FReplicationStateOperations::Quantize(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	// 对齐合规检查：Internal/External 都必须按 Descriptor 提供的对齐。
	check(IsAligned(DstInternalBuffer, Descriptor->InternalAlignment) && IsAligned(SrcExternalBuffer, Descriptor->ExternalAlignment));

	// 取出 Descriptor 中的成员描述数组。
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	// 逐成员 Quantize：每个成员根据自己 NetSerializer 的 Quantize 实现把 External 内存解码到 Internal 紧凑表示。
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		FNetQuantizeArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcExternalBuffer + MemberDescriptor.ExternalMemberOffset);
		Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);

		MemberSerializerDescriptor.Serializer->Quantize(Context, Args);
	}
}

void FReplicationStateOperations::QuantizeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	check(IsAligned(DstInternalBuffer, Descriptor->InternalAlignment) && IsAligned(SrcExternalBuffer, Descriptor->ExternalAlignment));

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
			FNetQuantizeArgs Args;
			Args.Version = 0;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcExternalBuffer + MemberDescriptor.ExternalMemberOffset);
			Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);

			Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
			Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;

			MemberSerializerDescriptor.Serializer->Quantize(Context, Args);
		}
	}
}

void FReplicationStateOperations::DequantizeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	check(IsAligned(SrcInternalBuffer, Descriptor->InternalAlignment) && IsAligned(DstExternalBuffer, Descriptor->ExternalAlignment));

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	// If adding code to only dequantize dirty states then special consideration is needed for init states.
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
			FNetDequantizeArgs Args;
			Args.Version = 0;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
			Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);

			// NOTE: Currently we only forward changemask info for fastarrays this is to avoid propagating it by accident where it does not belong
			if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::IsFastArrayReplicationState))
			{
				Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
				Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;
			}

			MemberSerializerDescriptor.Serializer->Dequantize(Context, Args);
		}
	}
}

void FReplicationStateOperations::Dequantize(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	check(IsAligned(SrcInternalBuffer, Descriptor->InternalAlignment) && IsAligned(DstExternalBuffer, Descriptor->ExternalAlignment));

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	// If adding code to only dequantize dirty states then special consideration is needed for init states.
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		FNetDequantizeArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
		Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);

		MemberSerializerDescriptor.Serializer->Dequantize(Context, Args);
	}
}

void FReplicationStateOperations::FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT StateInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	Private::FReplicationStateOperationsInternal::FreeDynamicState(Context, StateInternalBuffer, Descriptor);
}

bool FReplicationStateOperations::IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT Source0, const uint8* RESTRICT Source1, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	// $IRIS TODO Memcmp if quantized check and possible (no dynamic arrays)?
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetIsEqualArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		const uint32 MemberOffset = MemberDescriptor.InternalMemberOffset;
		MemberArgs.Source0 = NetSerializerValuePointer(Source0 + MemberOffset);
		MemberArgs.Source1 = NetSerializerValuePointer(Source1 + MemberOffset);
		MemberArgs.bStateIsQuantized = true;

		if (!Serializer->IsEqual(Context, MemberArgs))
		{
			return false;
		}
	}

	return true;
}

bool FReplicationStateOperations::Validate(FNetSerializationContext& Context, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	if (Descriptor == nullptr)
	{
		return false;
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetValidateArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = NetSerializerValuePointer(SrcExternalBuffer + MemberDescriptor.ExternalMemberOffset);

		if (!Serializer->Validate(Context, MemberArgs))
		{
			return false;
		}
	}

	return true;
}

static void OutputDescriptorMemberHashToString(FNetSerializationContext Context, uint32* HashBuffer, uint32 Indentation, FStringBuilderBase& StringBuilder, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();

	auto AppendMemberAndIndentation = [](FStringBuilderBase& StringBuilder, uint32 MemberIndex, uint32 Indentation)
	{
		while (Indentation--)
		{
			StringBuilder.Append(TEXT("    "));
		};
		StringBuilder.Appendf(TEXT("Member: %u "), MemberIndex);
	};

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		AppendMemberAndIndentation(StringBuilder, MemberIt, Indentation);

		// Traverse struct members as well.
		if (IsUsingStructNetSerializer(MemberSerializerDescriptor))
		{
			const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
			const FReplicationStateDescriptor* StructDescriptor = StructConfig->StateDescriptor;

			StringBuilder.Appendf(TEXT("%s\n"), ToCStr(MemberDebugDescriptors[MemberIt].DebugName));
			OutputDescriptorMemberHashToString(Context, HashBuffer, Indentation + 1, StringBuilder, SrcInternalBuffer + MemberDescriptor.InternalMemberOffset, StructDescriptor);
			return;	
		}
		else
		{
			// Reset writer as we want to capture this per member
			Writer.Seek(0U);

			FNetSerializeArgs Args;
			Args.Version = 0;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);

			MemberSerializerDescriptor.Serializer->Serialize(Context, Args);

			// Make sure last byte has well defined data as we include it in hash
			if (const uint32 BitsToFill = (8U - (Writer.GetPosBits() & 7U)) & 7U)
			{
				Writer.WriteBits(0U, BitsToFill);
			}

			Writer.CommitWrites();

			// Calculate and print hash 
			// Hash the serialized buffer
			const uint64 MemberDefaultHash = CityHash64((const char*)HashBuffer, Writer.GetPosBytes());
		
			StringBuilder.Appendf(TEXT("%s Serializer: %s Hash: 0x%" UINT64_x_FMT "\n"), ToCStr(MemberDebugDescriptors[MemberIt].DebugName), ToCStr(MemberSerializerDescriptor.Serializer->Name), MemberDefaultHash);
		}
	}
};

void FReplicationStateOperations::OutputDefaultStateMembersHashToString(UReplicationSystem* ReplicationSystem, FStringBuilderBase& StringBuilder, const FReplicationStateDescriptor* Descriptor)
{
	if (!ReplicationSystem)
	{
		return;
	}

	if (!Descriptor || !Descriptor->DefaultStateBuffer)
	{
		return;
	}

	// Setup a temporary buffer for this.
	constexpr uint32 BufferSize = 4096;
	TArray<uint32> TempBuffer;
	TempBuffer.SetNum(BufferSize/sizeof(uint32));

	FNetBitStreamWriter Writer;
	Writer.InitBytes(TempBuffer.GetData(), BufferSize);

	// Setup context to serialize default state
	FNetSerializationContext Context(&Writer);
	Private::FInternalNetSerializationContext InternalContext(ReplicationSystem);
	Context.SetInternalContext(&InternalContext);

	// Tell serializers we are serializing default state. It allows serializers to opt out of being part of the checksum by simply not serializing any data.
	Context.SetIsInitializingDefaultState(true);

	OutputDescriptorMemberHashToString(Context, TempBuffer.GetData(), 0, StringBuilder, Descriptor->DefaultStateBuffer, Descriptor);
}


void FReplicationStateOperations::Serialize(FNetSerializationContext& Context, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		FNetSerializeArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);

		MemberSerializerDescriptor.Serializer->Serialize(Context, Args);
	}
}

void FReplicationStateOperations::Deserialize(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		UE_ADD_READ_JOURNAL_ENTRY(Context, MemberDebugDescriptors[MemberIt].DebugName);

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		FNetDeserializeArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);

		MemberSerializerDescriptor.Serializer->Deserialize(Context, Args);
	}
}

void FReplicationStateOperations::SerializeDelta(FNetSerializationContext& Context, const uint8* RESTRICT SrcInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		FNetSerializeDeltaArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);
		Args.Prev = reinterpret_cast<NetSerializerValuePointer>(PrevInternalBuffer + MemberDescriptor.InternalMemberOffset);

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		MemberSerializerDescriptor.Serializer->SerializeDelta(Context, Args);
	}
}

void FReplicationStateOperations::DeserializeDelta(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		UE_ADD_READ_JOURNAL_ENTRY(Context, MemberDebugDescriptors[MemberIt].DebugName);

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];

		FNetDeserializeDeltaArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);
		Args.Prev = reinterpret_cast<NetSerializerValuePointer>(PrevInternalBuffer + MemberDescriptor.InternalMemberOffset);

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		MemberSerializerDescriptor.Serializer->DeserializeDelta(Context, Args);
	}
}


void FReplicationStateOperations::SerializeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const FReplicationStateMemberLifetimeConditionDescriptor* MemberLifetimeConditionDescriptors = Descriptor->MemberLifetimeConditionDescriptors;

	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
#if UE_NET_TRACE_ENABLED
			const int8 Condition = (MemberLifetimeConditionDescriptors != nullptr) ? MemberLifetimeConditionDescriptors[MemberIt].Condition : int8(ELifetimeCondition::COND_None);
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ConditionTraceScope, &Private::s_LifetimeConditionDebugNames[Condition], *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			if (MemberLifetimeConditionDescriptors == nullptr)
			{
				UE_NET_TRACE_EXIT_NAMED_SCOPE(ConditionTraceScope);
			}

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
#endif

			FNetSerializeArgs Args;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
			Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;
			Args.Version = 0;

			MemberSerializerDescriptor.Serializer->Serialize(Context, Args);
		}
	}
}

void FReplicationStateOperations::DeserializeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const FReplicationStateMemberLifetimeConditionDescriptor* MemberLifetimeConditionDescriptors = Descriptor->MemberLifetimeConditionDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
#if UE_NET_TRACE_ENABLED
			const int8 Condition = (MemberLifetimeConditionDescriptors != nullptr) ? MemberLifetimeConditionDescriptors[MemberIt].Condition : int8(ELifetimeCondition::COND_None);
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ConditionTraceScope, &Private::s_LifetimeConditionDebugNames[Condition], *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			if (MemberLifetimeConditionDescriptors == nullptr)
			{
				UE_NET_TRACE_EXIT_NAMED_SCOPE(ConditionTraceScope);
			}

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
#endif

			UE_ADD_READ_JOURNAL_ENTRY(Context, MemberDebugDescriptors[MemberIt].DebugName);

			FNetDeserializeArgs Args;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
			Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;
			Args.Version = 0;

			MemberSerializerDescriptor.Serializer->Deserialize(Context, Args);
		}
	}
}

void FReplicationStateOperations::SerializeDeltaWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const FReplicationStateMemberLifetimeConditionDescriptor* MemberLifetimeConditionDescriptors = Descriptor->MemberLifetimeConditionDescriptors;

	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
#if UE_NET_TRACE_ENABLED
			const int8 Condition = (MemberLifetimeConditionDescriptors != nullptr) ? MemberLifetimeConditionDescriptors[MemberIt].Condition : int8(ELifetimeCondition::COND_None);
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ConditionTraceScope, &Private::s_LifetimeConditionDebugNames[Condition], *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			if (MemberLifetimeConditionDescriptors == nullptr)
			{
				UE_NET_TRACE_EXIT_NAMED_SCOPE(ConditionTraceScope);
			}

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
#endif

			FNetSerializeDeltaArgs Args;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.Prev = reinterpret_cast<NetSerializerValuePointer>(PrevInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
			Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;
			Args.Version = 0;

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			MemberSerializerDescriptor.Serializer->SerializeDelta(Context, Args);
		}
	}
}

void FReplicationStateOperations::DeserializeDeltaWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	check(!Descriptor->IsInitState());
	Private::CheckChangeMask(Context, ChangeMask);
#endif

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const FReplicationStateMemberLifetimeConditionDescriptor* MemberLifetimeConditionDescriptors = Descriptor->MemberLifetimeConditionDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Descriptor->MemberChangeMaskDescriptors[MemberIt];
		const uint32 MemberChangeMaskOffset = ChangeMaskOffset + MemberChangeMaskDescriptor.BitOffset;

		if (ChangeMask.IsAnyBitSet(MemberChangeMaskOffset, MemberChangeMaskDescriptor.BitCount))
		{
#if UE_NET_TRACE_ENABLED
			const int8 Condition = (MemberLifetimeConditionDescriptors != nullptr) ? MemberLifetimeConditionDescriptors[MemberIt].Condition : int8(ELifetimeCondition::COND_None);
			UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(ConditionTraceScope, &Private::s_LifetimeConditionDebugNames[Condition], *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
			if (MemberLifetimeConditionDescriptors == nullptr)
			{
				UE_NET_TRACE_EXIT_NAMED_SCOPE(ConditionTraceScope);
			}

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
#endif

			UE_ADD_READ_JOURNAL_ENTRY(Context, MemberDebugDescriptors[MemberIt].DebugName);

			FNetDeserializeDeltaArgs Args;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.Prev = reinterpret_cast<NetSerializerValuePointer>(PrevInternalBuffer + MemberDescriptor.InternalMemberOffset);
			Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
			Args.ChangeMaskInfo.BitOffset = MemberChangeMaskOffset;
			Args.Version = 0;

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberSerializerDescriptor.Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			MemberSerializerDescriptor.Serializer->DeserializeDelta(Context, Args);
		}
	}
}

void FReplicationStateOperations::ApplyStruct(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	Private::InternalApplyStructProperty(Descriptor, DstExternalBuffer, SrcExternalBuffer);
}

bool FReplicationInstanceOperations::PollAndCopyPropertyData(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentTraits ExcludeTraits, EReplicationFragmentPollFlags PollOptions)
{
	bool bIsStateDirty = false;

	// @TODO Cache traits to avoid touching fragments
	const uint32 FragmentCount = InstanceProtocol->FragmentCount;
	for (uint32 StateIt = 0; StateIt < FragmentCount; ++StateIt)
	{
		// Only poll fragments with NeedsPoll and none of the ExcludedTraits set.
		const EReplicationFragmentTraits MaskedFragmentTraits = InstanceProtocol->FragmentData[StateIt].Traits & (EReplicationFragmentTraits::NeedsPoll | ExcludeTraits);
		if (MaskedFragmentTraits == EReplicationFragmentTraits::NeedsPoll)
		{
			bIsStateDirty |= InstanceProtocol->Fragments[StateIt]->PollReplicatedState(PollOptions);
		}
	}

	return bIsStateDirty;
}

bool FReplicationInstanceOperations::PollAndCopyPropertyData(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentPollFlags PollOptions)
{
	return PollAndCopyPropertyData(InstanceProtocol, EReplicationFragmentTraits::None, PollOptions);
}

bool FReplicationInstanceOperations::PollAndCopyObjectReferences(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentTraits RequiredTraits, EReplicationFragmentPollFlags PollOptions)
{
	bool bIsStateDirty = false;
	const EReplicationFragmentTraits ExpectedTraits = EReplicationFragmentTraits::HasObjectReference | RequiredTraits;

	const uint32 FragmentCount = InstanceProtocol->FragmentCount;
	for (uint32 StateIt = 0; StateIt < FragmentCount; ++StateIt)
	{
		const EReplicationFragmentTraits MaskedFragmentTraits = InstanceProtocol->FragmentData[StateIt].Traits & ExpectedTraits;
		if (MaskedFragmentTraits == ExpectedTraits)
		{			
			bIsStateDirty |= InstanceProtocol->Fragments[StateIt]->PollReplicatedState(PollOptions | EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC);
		}
	}

	return bIsStateDirty;
}

void FReplicationInstanceOperations::Quantize(FNetSerializationContext& Context, uint8* DstObjectStateBuffer, FNetBitStreamWriter* OutChangeMaskWriter, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{
	IRIS_PROFILER_SCOPE_VERBOSE(Quantize);

	check(InstanceProtocol && InstanceProtocol->FragmentCount == Protocol->ReplicationStateCount);

	FReplicationInstanceProtocol::FFragmentData* FragmentData = InstanceProtocol->FragmentData;
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;

	// Set up conditional change mask writer.
	// 若 Protocol 含 conditional changemask（lifetime conditionals），则也要把每个 state 的 conditional 段拼接进缓冲。
	// 这段 conditional mask 会被嵌入到 ObjectStateBuffer 的 GetConditionalChangeMaskOffset() 处。
	FNetBitStreamWriter ConditionalChangeMaskWriter;
	FNetBitStreamWriter* OutConditionalChangeMaskWriter = nullptr;
	if (EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasConditionalChangeMask))
	{
		ConditionalChangeMaskWriter.InitBytes(DstObjectStateBuffer + Protocol->GetConditionalChangeMaskOffset(), FNetBitArrayView::CalculateRequiredWordCount(Protocol->ChangeMaskBitCount)*sizeof(FNetBitArrayView::StorageWordType));
		OutConditionalChangeMaskWriter = &ConditionalChangeMaskWriter;
	}

	// 全量 Quantize：依次走每个 ReplicationState（按 Descriptor->InternalAlignment 对齐）。
	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;	
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		FReplicationStateOperations::Quantize(Context, CurrentInternalStateBuffer, FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);

		// Append changemask data to bitstreams
		Private::AppendMemberChangeMasks(OutChangeMaskWriter, OutConditionalChangeMaskWriter, FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}

	OutChangeMaskWriter->CommitWrites();
	if (OutConditionalChangeMaskWriter != nullptr)
	{
		OutConditionalChangeMaskWriter->CommitWrites();
	}
}

void FReplicationInstanceOperations::QuantizeIfDirty(FNetSerializationContext& Context, uint8* DstObjectStateBuffer, FNetBitStreamWriter* OutChangeMaskWriter, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{
	// QuantizeIfDirty —— 仅 quantize 标脏状态（生产路径）。
	// 关键差异（vs Quantize）：在 AppendMemberChangeMasks 返回 true 时才执行 Quantize；同时若 cvar 允许，对非 init state
	// 还会进一步只 quantize ChangeMask 中真正脏的成员（QuantizeWithMask）。
	IRIS_PROFILER_SCOPE_VERBOSE(QuantizeIfDirty);

	check(InstanceProtocol && InstanceProtocol->FragmentCount == Protocol->ReplicationStateCount);

	FReplicationInstanceProtocol::FFragmentData* FragmentData = InstanceProtocol->FragmentData;
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;

	// Set up conditional change mask writer.
	FNetBitStreamWriter ConditionalChangeMaskWriter;
	FNetBitStreamWriter* OutConditionalChangeMaskWriter = nullptr;
	if (EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasConditionalChangeMask))
	{
		ConditionalChangeMaskWriter.InitBytes(DstObjectStateBuffer + Protocol->GetConditionalChangeMaskOffset(), FNetBitArrayView::CalculateRequiredWordCount(Protocol->ChangeMaskBitCount)*sizeof(FNetBitArrayView::StorageWordType));
		OutConditionalChangeMaskWriter = &ConditionalChangeMaskWriter;
	}

	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;	
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		// Append changemask data to bitstreams and quantize state if mask is dirty
		if (Private::AppendMemberChangeMasks(OutChangeMaskWriter, OutConditionalChangeMaskWriter, FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor))
		{
			IRIS_PROFILER_PROTOCOL_NAME(CurrentDescriptor->DebugName->Name);

			if (CurrentDescriptor->IsInitState() || !bOnlyQuantizeDirtyMembers)
			{
				FReplicationStateOperations::Quantize(Context, CurrentInternalStateBuffer, FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
			}
			else
			{
				FNetBitArrayView ChangeMask = UE::Net::Private::GetMemberChangeMask(FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
				Context.SetChangeMask(&ChangeMask);
				FReplicationStateOperations::QuantizeWithMask(Context, ChangeMask, 0, CurrentInternalStateBuffer, FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
			}
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}

	OutChangeMaskWriter->CommitWrites();
	if (OutConditionalChangeMaskWriter != nullptr)
	{
		OutConditionalChangeMaskWriter->CommitWrites();
	}
}

void FReplicationInstanceOperations::ResetDirtiness(const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{
	check(InstanceProtocol && InstanceProtocol->FragmentCount == Protocol->ReplicationStateCount);

	FReplicationInstanceProtocol::FFragmentData* FragmentData = InstanceProtocol->FragmentData;
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;

	for (uint32 StateIt = 0, StateEndIt = Protocol->ReplicationStateCount; StateIt < StateEndIt; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];
		Private::ResetMemberChangeMasks(FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
		Private::ResetMemberPollMask(FragmentData[StateIt].ExternalSrcBuffer, CurrentDescriptor);
	}
}

void FReplicationInstanceOperations::DequantizeAndApply(FNetSerializationContext& NetSerializationContext, const FDequantizeAndApplyParameters& Parameters)
{
	using namespace UE::Net::Private;

	// 接收侧入口（客户端用）：把内部量化状态 dequantize 出来 → 调用每个 Fragment 的 Apply / RepNotify / 遗留回调。
	// 真正实现委托给 FDequantizeAndApplyHelper（Initialize / ApplyAndCallLegacyFunctions / Deinitialize 三步）。
	// Helper 负责：临时分配 ExternalState、按 ChangeMask partial dequantize、ApplyReplicatedState、CollectOwner 与 Pre/PostNetReceive、
	// CallRepNotifies、最后 Destruct 临时 ExternalState 防止动态内存泄漏。
	FDequantizeAndApplyHelper::FContext* Context = FDequantizeAndApplyHelper::Initialize(NetSerializationContext, Parameters);

	FDequantizeAndApplyHelper::ApplyAndCallLegacyFunctions(Context, NetSerializationContext);

	// We need to destruct temporary states (as properties might have allocated memory outside of our temporary allocator)
	// 临时 state 中的 UProperty 可能持有"非线性 allocator"分配的内存（FString/TArray 等），必须 Destruct 才能正确释放。
	FDequantizeAndApplyHelper::Deinitialize(Context);
}

void FReplicationInstanceOperations::DequantizeAndApply(FNetSerializationContext& Context, FMemStackBase& InAllocator, const uint32* ChangeMaskData, const FReplicationInstanceProtocol* InstanceProtocol, const uint8* SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	FDequantizeAndApplyParameters Parameters;
	Parameters.Allocator = &InAllocator;
	Parameters.ChangeMaskData = ChangeMaskData;
	Parameters.InstanceProtocol = InstanceProtocol;
	Parameters.SrcObjectStateBuffer = SrcObjectStateBuffer;
	Parameters.Protocol = Protocol;

	DequantizeAndApply(Context, Parameters);
}

void FReplicationProtocolOperations::Serialize(FNetSerializationContext& Context, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	const uint8* CurrentInternalStateBuffer = SrcObjectStateBuffer;

	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		FReplicationStateOperations::Serialize(Context, CurrentInternalStateBuffer, CurrentDescriptor);
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationInstanceOperations::OutputInternalStateToString(FNetSerializationContext& NetSerializationContext, FStringBuilderBase& StringBuilder, const uint32* ChangeMaskData, const uint8* InternalStateBuffer, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{
	// Iterate over fragments
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	FReplicationFragment* const * ReplicationFragments = InstanceProtocol->Fragments;	

	// We need to accumulate the offsets and alignment of each internal state
	const uint8* CurrentInternalStateBuffer = InternalStateBuffer;

	for (uint32 StateIt = 0, StateEndIt = Protocol->ReplicationStateCount; StateIt != StateEndIt; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];
		const FReplicationFragment* CurrentFragment = ReplicationFragments[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		FReplicationStateApplyContext ReplicationStateToStringContext;
		ReplicationStateToStringContext.NetSerializationContext = &NetSerializationContext;
		ReplicationStateToStringContext.Descriptor = CurrentDescriptor;		
		ReplicationStateToStringContext.bIsInit = NetSerializationContext.IsInitState();

		// Dequantize state data
		if (!EnumHasAnyFlags(CurrentFragment->GetTraits(), EReplicationFragmentTraits::HasPersistentTargetStateBuffer))
		{
			// Allocate buffer for temporary state and construct the external state
			uint8* StateBuffer = (uint8*)FMemory::Malloc(CurrentDescriptor->ExternalSize, CurrentDescriptor->ExternalAlignment);
			CurrentDescriptor->ConstructReplicationState(StateBuffer, CurrentDescriptor);
	
			// Dequantize state data
			FReplicationStateOperations::Dequantize(NetSerializationContext, StateBuffer, (uint8*)CurrentInternalStateBuffer, CurrentDescriptor);

			// Pass in the dequantized state buffer
			ReplicationStateToStringContext.StateBufferData.ExternalStateBuffer = StateBuffer;

			CurrentFragment->ReplicatedStateToString(StringBuilder, ReplicationStateToStringContext, EReplicationStateToStringFlags::None);

			// Free buffer
			CurrentDescriptor->DestructReplicationState(StateBuffer, CurrentDescriptor);
			FMemory::Free(StateBuffer);
		}
		else
		{
			// Pass in raw internal statebuffer
			ReplicationStateToStringContext.StateBufferData.ChangeMaskData = nullptr;
			ReplicationStateToStringContext.StateBufferData.RawStateBuffer = CurrentInternalStateBuffer;

			CurrentFragment->ReplicatedStateToString(StringBuilder, ReplicationStateToStringContext, EReplicationStateToStringFlags::None);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationInstanceOperations::OutputInternalDefaultStateMemberHashesToString(UReplicationSystem* ReplicationSystem, FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments)
{
	// Iterate over fragments
	for (int32 Index=0; Index < Fragments.Num(); ++Index)
	{
		const FReplicationFragmentInfo& FragmentInfo = Fragments[Index];

		const FReplicationStateDescriptor* CurrentDescriptor = FragmentInfo.Descriptor;
		const FReplicationFragment* CurrentFragment = FragmentInfo.Fragment;

		StringBuilder.Appendf(TEXT("Printing default state hash for members of [%d/%d] Fragment: %s DescriptorId: 0x%" UINT64_x_FMT " DefaultStateHash: 0x%" UINT64_x_FMT "\n"), 
			Index+1, Fragments.Num(), ToCStr(CurrentDescriptor->DebugName), CurrentDescriptor->DescriptorIdentifier.Value, CurrentDescriptor->DescriptorIdentifier.DefaultStateHash);
		FReplicationStateOperations::OutputDefaultStateMembersHashToString(ReplicationSystem, StringBuilder, CurrentDescriptor);
	}	
}

void FReplicationInstanceOperations::OutputInternalDefaultStateToString(FNetSerializationContext& NetSerializationContext, FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments)
{
	// Iterate over fragments
	for (int32 Index=0; Index < Fragments.Num(); ++Index)
	{
		const FReplicationFragmentInfo& FragmentInfo = Fragments[Index];

		const FReplicationStateDescriptor* CurrentDescriptor = FragmentInfo.Descriptor;
		const FReplicationFragment* CurrentFragment = FragmentInfo.Fragment;

		FReplicationStateApplyContext ReplicationStateToStringContext;
		ReplicationStateToStringContext.NetSerializationContext = &NetSerializationContext;
		ReplicationStateToStringContext.Descriptor = CurrentDescriptor;
		ReplicationStateToStringContext.bIsInit = NetSerializationContext.IsInitState();

		StringBuilder.Appendf(TEXT("[%d/%d] Fragment: %s DescriptorId: 0x%" UINT64_x_FMT " DefaultStateHash: 0x%" UINT64_x_FMT "\n"), 
			Index+1, Fragments.Num(), ToCStr(CurrentDescriptor->DebugName), CurrentDescriptor->DescriptorIdentifier.Value, CurrentDescriptor->DescriptorIdentifier.DefaultStateHash);

		// Dequantize state data
		if (!EnumHasAnyFlags(CurrentFragment->GetTraits(), EReplicationFragmentTraits::HasPersistentTargetStateBuffer))
		{
			// Allocate buffer for temporary state and construct the external state
			uint8* StateBuffer = (uint8*)FMemory::Malloc(CurrentDescriptor->ExternalSize, CurrentDescriptor->ExternalAlignment);
			CurrentDescriptor->ConstructReplicationState(StateBuffer, CurrentDescriptor);

			// Dequantize state data
			FReplicationStateOperations::Dequantize(NetSerializationContext, StateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);

			// Pass in the dequantized state buffer
			ReplicationStateToStringContext.StateBufferData.ExternalStateBuffer = StateBuffer;

			CurrentFragment->ReplicatedStateToString(StringBuilder, ReplicationStateToStringContext, EReplicationStateToStringFlags::None);

			// Free buffer
			CurrentDescriptor->DestructReplicationState(StateBuffer, CurrentDescriptor);
			FMemory::Free(StateBuffer);
		}
		else
		{
			// Pass in raw internal statebuffer
			ReplicationStateToStringContext.StateBufferData.ChangeMaskData = nullptr;
			ReplicationStateToStringContext.StateBufferData.RawStateBuffer = CurrentDescriptor->DefaultStateBuffer;

			CurrentFragment->ReplicatedStateToString(StringBuilder, ReplicationStateToStringContext, EReplicationStateToStringFlags::None);
		}
	}
}

void FReplicationProtocolOperations::Deserialize(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;

	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		UE_ADD_READ_JOURNAL_ENTRY(Context, CurrentDescriptor->DebugName);

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		FReplicationStateOperations::Deserialize(Context, CurrentInternalStateBuffer, CurrentDescriptor);
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationProtocolOperations::SerializeWithMask(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	// 对象级带 ChangeMask 序列化：Writer 主路径。
	// 流程：
	//   1) 把整段 ChangeMask 用 SparseBitArray 写入 BitStream（多数对象大多为 0）；
	//   2) 逐 Descriptor 调用 SerializeWithMask；init state 仅在 IsInitState() 时序列化。
	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	const uint8* CurrentInternalStateBuffer = SrcObjectStateBuffer;

	// Setup changemask
	const FNetBitArrayView ChangeMask = MakeNetBitArrayView(ChangeMaskData, Protocol->ChangeMaskBitCount);
	uint32 CurrentChangeMaskBitOffset = 0;
	Context.SetChangeMask(&ChangeMask);

	// We currently write the changemask for the the complete protocol
	// This will later either be RLE compressed or written in chunks per ReplicationState
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		WriteSparseBitArray(Writer, ChangeMaskData, Protocol->ChangeMaskBitCount);
	}

	const bool bIsInitState = Context.IsInitState();
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		// Only send init state at init
		if (CurrentDescriptor->IsInitState())
		{
			if (bIsInitState)
			{
				UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
				FReplicationStateOperations::Serialize(Context, CurrentInternalStateBuffer, CurrentDescriptor);
			}
		}
		else
		{
			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
			FReplicationStateOperations::SerializeWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, CurrentDescriptor);
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}

	// Remove dangling reference to ChangeMask on the stack
	Context.SetChangeMask(nullptr);
}

void FReplicationProtocolOperations::DeserializeWithMask(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;
	
	// We might have garbage in the buffer, we do not need to clear it all but we must reset the padding bits
	FNetBitArrayView ChangeMask(DstChangeMaskData, Protocol->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
	ChangeMask.ClearPaddingBits();
	Context.SetChangeMask(&ChangeMask);

	// Read the ChangeMask
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		UE_ADD_READ_JOURNAL_ENTRY(Context, TEXT("ChangeMask"));
		ReadSparseBitArray(Context.GetBitStreamReader(), DstChangeMaskData, Protocol->ChangeMaskBitCount);
	}

	const bool bIsInitState = Context.IsInitState();
	uint32 CurrentChangeMaskBitOffset = 0;
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		UE_ADD_READ_JOURNAL_ENTRY(Context, CurrentDescriptor->DebugName);
		
		if (CurrentDescriptor->IsInitState())
		{
			if (bIsInitState)
			{
				UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
				FReplicationStateOperations::Deserialize(Context, CurrentInternalStateBuffer, CurrentDescriptor);
			}
		}
		else
		{
			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
			FReplicationStateOperations::DeserializeWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, CurrentDescriptor);
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}
}

void FReplicationProtocolOperations::FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	if (!EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasDynamicState))
	{
		return;
	}

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = SrcObjectStateBuffer;

	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		if (EnumHasAnyFlags(CurrentDescriptor->Traits, EReplicationStateTraits::HasDynamicState))
		{
			Private::FReplicationStateOperationsInternal::FreeDynamicState(Context, CurrentInternalStateBuffer, CurrentDescriptor);
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationProtocolOperations::SerializeInitialStateWithMask(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	// 初始状态特殊路径：相对默认状态 delta 编码，让"接近默认值"的字段几乎不耗带宽。
	// 受 cvar net.Iris.DeltaCompressInitialState 控制；关闭时退化为普通 SerializeWithMask。
	if (!bDeltaCompressInitialState)
	{
		SerializeWithMask(Context, ChangeMaskData, SrcObjectStateBuffer, Protocol);
		return;
	}

	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	const uint8* CurrentInternalStateBuffer = SrcObjectStateBuffer;

	// Setup changemask
	const FNetBitArrayView ChangeMask = MakeNetBitArrayView(ChangeMaskData, Protocol->ChangeMaskBitCount);
	uint32 CurrentChangeMaskBitOffset = 0;
	Context.SetChangeMask(&ChangeMask);

	// We currently write the changemask for the complete protocol
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		WriteSparseBitArray(Writer, ChangeMaskData, Protocol->ChangeMaskBitCount, ESparseBitArraySerializationHint::ContainsMostlyOnes);
	}

	check(Context.IsInitState());
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

		if (CurrentDescriptor->IsInitState())
		{
			FReplicationStateOperations::SerializeDelta(Context, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}
		else
		{
			FReplicationStateOperations::SerializeDeltaWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}

	// Remove dangling reference to ChangeMask on the stack
	Context.SetChangeMask(nullptr);
}

void FReplicationProtocolOperations::DeserializeInitialStateWithMask(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	if (!bDeltaCompressInitialState)
	{
		DeserializeWithMask(Context, DstChangeMaskData, DstObjectStateBuffer, Protocol);
		return;
	}

	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;

	// We might have garbage in the buffer, we do not need to clear it all but we must reset the padding bits
	FNetBitArrayView ChangeMask(DstChangeMaskData, Protocol->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
	ChangeMask.ClearPaddingBits();
	Context.SetChangeMask(&ChangeMask);

	// Read the ChangeMask
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		ReadSparseBitArray(Context.GetBitStreamReader(), DstChangeMaskData, Protocol->ChangeMaskBitCount, ESparseBitArraySerializationHint::ContainsMostlyOnes);
	}

	check(Context.IsInitState());

	uint32 CurrentChangeMaskBitOffset = 0;
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		// Since we might not serialize all members we must initialize the state with the default state.
		if (CurrentDescriptor->InternalSize > 0)
		{
			Private::FReplicationStateOperationsInternal::CloneQuantizedState(Context, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		if (CurrentDescriptor->IsInitState())
		{
			FReplicationStateOperations::DeserializeDelta(Context, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}
		else
		{
			FReplicationStateOperations::DeserializeDeltaWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}

	// Remove dangling reference to ChangeMask on the stack
	Context.SetChangeMask(nullptr);
}

void FReplicationProtocolOperations::SerializeWithMaskDelta(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const uint8* RESTRICT PrevObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	const uint8* CurrentInternalStateBuffer = SrcObjectStateBuffer;
	const uint8* PrevInternalStateBuffer = PrevObjectStateBuffer;

	// Setup changemask
	const FNetBitArrayView ChangeMask = MakeNetBitArrayView(ChangeMaskData, Protocol->ChangeMaskBitCount);
	uint32 CurrentChangeMaskBitOffset = 0;
	Context.SetChangeMask(&ChangeMask);

	// We currently write the changemask for the complete protocol
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		WriteSparseBitArray(Writer, ChangeMaskData, Protocol->ChangeMaskBitCount);
	}

	const bool bIsInitState = Context.IsInitState();
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		PrevInternalStateBuffer = Align(PrevInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

		if (CurrentDescriptor->IsInitState())
		{
			if (bIsInitState)
			{
				FReplicationStateOperations::SerializeDelta(Context, CurrentInternalStateBuffer, PrevInternalStateBuffer, CurrentDescriptor);
			}
		}
		else
		{
			FReplicationStateOperations::SerializeDeltaWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, PrevInternalStateBuffer, CurrentDescriptor);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		PrevInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}

	// Remove dangling reference to ChangeMask on the stack
	Context.SetChangeMask(nullptr);
}

void FReplicationProtocolOperations::DeserializeWithMaskDelta(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT PrevObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = DstObjectStateBuffer;
	const uint8* PrevInternalStateBuffer = PrevObjectStateBuffer;

	// We might have garbage in the buffer, we do not need to clear it all but we must reset the padding bits
	FNetBitArrayView ChangeMask(DstChangeMaskData, Protocol->ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
	ChangeMask.ClearPaddingBits();
	Context.SetChangeMask(&ChangeMask);

	// Read the ChangeMask
	{
		UE_NET_TRACE_SCOPE(ChangeMasks, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		UE_ADD_READ_JOURNAL_ENTRY(Context, TEXT("ChangeMask"));
		ReadSparseBitArray(Context.GetBitStreamReader(), DstChangeMaskData, Protocol->ChangeMaskBitCount);
	}

	const bool bIsInitState = Context.IsInitState();
	uint32 CurrentChangeMaskBitOffset = 0;
	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		PrevInternalStateBuffer = Align(PrevInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(CurrentDescriptor->DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		UE_ADD_READ_JOURNAL_ENTRY(Context, CurrentDescriptor->DebugName);
		if (CurrentDescriptor->IsInitState())
		{
			if (bIsInitState)
			{
				FReplicationStateOperations::DeserializeDelta(Context, CurrentInternalStateBuffer, PrevInternalStateBuffer, CurrentDescriptor);
			}
		}
		else
		{
			FReplicationStateOperations::DeserializeDeltaWithMask(Context, ChangeMask, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, PrevInternalStateBuffer, CurrentDescriptor);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		PrevInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}
}

void FReplicationProtocolOperations::InitializeFromDefaultState(FNetSerializationContext& Context, uint8* RESTRICT StateBuffer, const FReplicationProtocol* Protocol)
{
	// Nothing to serialize
	if (Protocol->InternalTotalSize == 0U)
	{
		return;
	}

	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = StateBuffer;

	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		if (CurrentDescriptor->InternalSize > 0)
		{
			ensure(CurrentDescriptor->DefaultStateBuffer != nullptr);
			Private::FReplicationStateOperationsInternal::CloneQuantizedState(Context, CurrentInternalStateBuffer, CurrentDescriptor->DefaultStateBuffer, CurrentDescriptor);
		}

		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}


bool FReplicationProtocolOperations::IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT Source0, const uint8* RESTRICT Source1, const FReplicationProtocol* Protocol)
{
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	const uint8* Source0InternalStateBuffer = Source0;
	const uint8* Source1InternalStateBuffer = Source1;

	for (uint32 StateIt = 0; StateIt < Protocol->ReplicationStateCount; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		Source0InternalStateBuffer = Align(Source0InternalStateBuffer, CurrentDescriptor->InternalAlignment);
		Source1InternalStateBuffer = Align(Source1InternalStateBuffer, CurrentDescriptor->InternalAlignment);

		if (!FReplicationStateOperations::IsEqualQuantizedState(Context, Source0InternalStateBuffer, Source1InternalStateBuffer, CurrentDescriptor))
		{
			return false;
		}

		Source0InternalStateBuffer += CurrentDescriptor->InternalSize;
		Source1InternalStateBuffer += CurrentDescriptor->InternalSize;
	}

	return true;
}

void ResetLifetimeConditionDebugNames()
{
	for (FNetDebugName& Name : Private::s_LifetimeConditionDebugNames)
	{
		Name.DebugNameId = 0;
	}
}

}

namespace UE::Net::Private
{

static bool AppendMemberChangeMasks(FNetBitStreamWriter* ChangeMaskWriter, FNetBitStreamWriter* ConditionalChangeMaskWriter, uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	// 把单个 ReplicationState 的 ChangeMask 拼到对象级 ChangeMaskWriter 中。
	// 同时按需拼接 ConditionalChangeMask（仅 lifetime conditionals 状态）。
	const uint32 BitCount = Descriptor->ChangeMaskBitCount;
	if (BitCount)
	{
		// Append change mask bits
		// ChangeMask 在 ExternalStateBuffer 的 GetChangeMaskOffset() 位置存放。<=32 位走 WriteBits 快路径，否则用 WriteBitStream 流式写入。
		FNetBitArrayView::StorageWordType* ChangeMaskStorage = reinterpret_cast<FNetBitArrayView::StorageWordType*>(StateBuffer + Descriptor->GetChangeMaskOffset());
		if (BitCount <= 32)
		{
			ChangeMaskWriter->WriteBits(*ChangeMaskStorage, BitCount);
		}
		else
		{
			constexpr uint32 SrcStreamBitOffset = 0U;
			ChangeMaskWriter->WriteBitStream(ChangeMaskStorage, SrcStreamBitOffset, BitCount);
		}

		// 处理 conditional 段
		if (ConditionalChangeMaskWriter != nullptr)
		{
			if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
			{
				FNetBitArrayView::StorageWordType* ConditionalChangeMaskStorage = reinterpret_cast<FNetBitArrayView::StorageWordType*>(StateBuffer + Descriptor->GetConditionalChangeMaskOffset());

				// Append conditional change mask bits.
				// The conditionals are on or off rather than tracking dirtiness so we do not reset them.
				// conditional bits 是"开/关"语义而非脏跟踪，因此 Quantize 时不会被清零。
				if (BitCount <= 32u)
				{
					ConditionalChangeMaskWriter->WriteBits(*ConditionalChangeMaskStorage, BitCount);
				}
				else
				{
					constexpr uint32 BitOffset = 0;
					ConditionalChangeMaskWriter->WriteBitStream(ConditionalChangeMaskStorage, BitOffset, BitCount);
				}
			}
			else
			{
				// Skip past our non-existing conditional mask.
				// 该 state 没有 conditional mask（普通成员），但 conditional writer 是按整个对象一次性分配的，
				// 必须把"虚位"留出来（Seek 跳过），保持后续 state 的 conditional 段对齐。
				ConditionalChangeMaskWriter->Seek(ConditionalChangeMaskWriter->GetPosBits() + BitCount);
			}
		}
	}

	// 返回该 state 是否脏（由 ReplicationStateHeader 的 dirty bit 决定，不依赖 ChangeMask 本身——
	// 因为 push-based 状态可能 ChangeMask 全 0 但仍因 dirty bit 被认为脏）。
	FReplicationStateHeader& ReplicationStateHeader = Private::GetReplicationStateHeader(StateBuffer, Descriptor);
	return FReplicationStateHeaderAccessor::GetIsStateDirty(ReplicationStateHeader);
}

static void ResetMemberChangeMasks(uint8* ExternalStateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	FNetBitArrayView::StorageWordType* ChangeMaskStorage = reinterpret_cast<FNetBitArrayView::StorageWordType*>(ExternalStateBuffer + Descriptor->GetChangeMaskOffset());
	const uint32 BitCount = Descriptor->ChangeMaskBitCount;

	// Reset the changemask. We assume it's fine to clear any padding bits.
	FNetBitArrayView MemberChangeMask(ChangeMaskStorage, BitCount, FNetBitArrayView::ResetOnInit);

	// Reset state dirtiness.
	FReplicationStateHeader& ReplicationStateHeader = Private::GetReplicationStateHeader(ExternalStateBuffer, Descriptor);
	Private::FReplicationStateHeaderAccessor::ClearAllStateIsDirty(ReplicationStateHeader);
}

}
