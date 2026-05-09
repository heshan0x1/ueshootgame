// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StructNetSerializer.cpp —— Iris 中"嵌套 struct 容器"递归 dispatch 主循环
// -----------------------------------------------------------------------------
// FStructNetSerializer 是 Iris 序列化系统的核心 dispatcher 之一：
// 它本身不知道 struct 的 layout，全部通过 Config->StateDescriptor 中的
// `MemberDescriptors / MemberSerializerDescriptors / MemberTraitsDescriptors`
// 三个并行数组逐成员调用对应的 FNetSerializer 完成。
//
// 为什么是 dispatcher（而非真正"类型"）：
//   * 所有 USTRUCT 都把整段内存视作连续 layout，每个 member 有
//     ExternalMemberOffset（真实 SourceType 内偏移） + InternalMemberOffset
//     （量化态 Quantized layout 内偏移）；
//   * Member 自带 Serializer 指针（FNetSerializer*）和 SerializerConfig；
//   * 所以 Struct = 把 Args.Source/Target 加上 offset 后递归 dispatch 给
//     member.Serializer 即可。
//
// 关键 trait（构造时的 constexpr）：
//   * bIsForwardingSerializer = true —— 标记"我自己只是转发，没有底层数据"，
//     NetSerializerBuilder 会要求所有函数都得显式实现，缺一个就 assert。
//   * SourceType = void —— 没有专属类型，依赖 Descriptor。
//   * 通过 EReplicationStateTraits::HasDynamicState/HasObjectReference 等
//     描述符 trait，判断要不要执行 CloneDynamicState/FreeDynamicState/
//     CollectNetReferences 等可选步骤（避免无谓遍历）。
//
// 12 个公开函数：Serialize / Deserialize / Quantize / Dequantize / IsEqual /
// Validate / CloneDynamicState / FreeDynamicState / CollectNetReferences /
// SerializeDelta / DeserializeDelta（Apply 由上层 ReplicationStateOperations
// 处理，本 dispatcher 不再单独实现）。
// =============================================================================

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/Serialization/InternalNetSerializerUtils.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net
{

// FStructNetSerializer ——
// "通用 struct 容器"的 dispatcher 实现 struct。所有成员均为 static，
// 通过 UE_NET_IMPLEMENT_SERIALIZER 在底部生成 FNetSerializer singleton。
struct FStructNetSerializer
{
public:
	static const uint32 Version = 0;

	// 标记自己是 forwarding serializer：BuilderSFINAE 不会用 default 实现兜底，
	// 必须每个函数都显式给出。Triggers asserts if a function is missing.
	static constexpr bool bIsForwardingSerializer = true; // Triggers asserts if a function is missing

	// SourceType 用 void —— Struct 本身不绑定具体类型，layout 由 Config 中的
	// StateDescriptor 决定。
	typedef void SourceType; // Dummy

	typedef FStructNetSerializerConfig ConfigType;

	// 12 项契约函数 —— 全部按"逐成员 dispatch"展开。
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	// "拷贝单个成员"的辅助函数：先 Free 旧 dynamic state、Memcpy 整段量化态、
	// 再 Clone dynamic state。仅在 SerializeDelta 走 IsEqual 短路 + 拷贝旧值时使用。
	static void CloneStructMember(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
};

// 注册 FStructNetSerializer 全局 singleton（含 default Config 等元数据）。
UE_NET_IMPLEMENT_SERIALIZER(FStructNetSerializer);

// -----------------------------------------------------------------------------
// Serialize —— 把 quantized 状态逐成员写入比特流
// -----------------------------------------------------------------------------
void FStructNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	// 取出 struct 描述符。Member 三件套并行数组：Descriptor / Serializer / Debug。
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;

	const uint32 MemberCount = Descriptor->MemberCount;

	// 逐成员 dispatch：把 Args.Source 加上量化态内偏移交给该成员的 Serializer。
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetSerializeArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		// Serialize 阶段输入是 quantized 内存，所以走 InternalMemberOffset。
		MemberArgs.Source = Args.Source + MemberDescriptor.InternalMemberOffset;
		// Currently we pass on the changemask info unmodified to support fastarrays, but if we decide to support other serializers utilizing additional changemask we need to extend this
		// 直接透传外层 ChangeMaskInfo，支持 FastArray 子结构二级 ChangeMask。
		MemberArgs.ChangeMaskInfo = Args.ChangeMaskInfo;

		// NetTrace 调试范围：成员名 + Serializer 名（按详细级别分两条）。
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		Serializer->Serialize(Context, MemberArgs);
	}
}

// -----------------------------------------------------------------------------
// Deserialize —— Serialize 反向流程，错误立即返回，外层 BatchReader 会回滚。
// -----------------------------------------------------------------------------
void FStructNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetDeserializeArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Target = Args.Target + MemberDescriptor.InternalMemberOffset;
		// Currently we pass on the changemask info unmodified to support fastarrays, but if we decide to support other serializers utilizing additional changemask we need to extend this
		MemberArgs.ChangeMaskInfo = Args.ChangeMaskInfo;

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		Serializer->Deserialize(Context, MemberArgs);
		if (Context.HasErrorOrOverflow())
		{
			// $IRIS TODO Provide information which member is failing to deserialize (though could be red herring)
			return;
		}
	}
}

// -----------------------------------------------------------------------------
// SerializeDelta —— 带 baseline 比较的增量编码
// -----------------------------------------------------------------------------
// 优化：对"嵌套 struct 成员"先 IsEqual(quantized)，相等则只写 1 bit "true"
// 跳过；不相等则写 1 bit "false" 并递归调用其 SerializeDelta。
// 这是 backward-compat 模式下唯一的"成员级跳写"机制。
void FStructNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	/**
	  * $IRIS TODO UE-130963 The struct could hint whether we should check for equality before serializing.
	  * If the outer replication state already has a bit for state changes then this would be unnecessary,
	  * but if this struct itself contains struct members we'd want those to have hints. The solution
	  * would be to have a check for structs when a member is serialized and check and write whether the
	  * structs are equal before recursing into this function if they are not.
	  */

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;

	const uint32 MemberCount = Descriptor->MemberCount;

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Serializer->Name, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		if (IsStructNetSerializer(Serializer))
		{
			// 仅当成员本身就是 struct 时才走"先比 IsEqual + 写 1 bit 标记"的短路：
			// 这样可以最大化压缩"局部完全相同的子 struct"场景。
			FNetIsEqualArgs MemberEqualArgs;
			MemberEqualArgs.bStateIsQuantized = true;
			MemberEqualArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			MemberEqualArgs.Source0 = Args.Source + MemberDescriptor.InternalMemberOffset;
			MemberEqualArgs.Source1 = Args.Prev + MemberDescriptor.InternalMemberOffset;
			MemberEqualArgs.ChangeMaskInfo = Args.ChangeMaskInfo;
			if (Writer->WriteBool(IsEqual(Context, MemberEqualArgs)))
			{
				continue;
			}
		}

		{
			FNetSerializeDeltaArgs MemberArgs;
			MemberArgs.Version = 0;
			MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			MemberArgs.Source = Args.Source + MemberDescriptor.InternalMemberOffset;
			MemberArgs.Prev = Args.Prev + MemberDescriptor.InternalMemberOffset;
			// Currently we pass on the changemask info unmodified to support fastarrays, but if we decide to support other serializers utilizing additional changemask we need to extend this
			MemberArgs.ChangeMaskInfo = Args.ChangeMaskInfo;

			Serializer->SerializeDelta(Context, MemberArgs);
		}
	}
}

// -----------------------------------------------------------------------------
// DeserializeDelta —— SerializeDelta 反向流程
// -----------------------------------------------------------------------------
// 与发送端对称：嵌套 struct 成员先读 1 bit "isSame"。若相同则把 Prev 的 dynamic
// state 拷贝到 Target；否则递归调用成员的 DeserializeDelta。
void FStructNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
	const FReplicationStateMemberDebugDescriptor* MemberDebugDescriptors = Descriptor->MemberDebugDescriptors;

	const uint32 MemberCount = Descriptor->MemberCount;

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(MemberDebugDescriptors[MemberIt].DebugName, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);
		UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Serializer->Name, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		if (IsStructNetSerializer(Serializer))
		{
			// 读 1 bit："上次相比是否相等"。
			if (Reader->ReadBool())
			{
				// 相等：把 Prev 的整段量化态 + dynamic state 拷贝到 Target。
				FNetCloneDynamicStateArgs CloneArgs;
				CloneArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
				CloneArgs.Version = Args.Version;
				CloneArgs.Source = Args.Prev + MemberDescriptor.InternalMemberOffset;
				CloneArgs.Target = Args.Target + MemberDescriptor.InternalMemberOffset;
				CloneStructMember(Context, CloneArgs);
				continue;
			}
		}

		{
			FNetDeserializeDeltaArgs MemberArgs;
			MemberArgs.Version = 0;
			MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			MemberArgs.Target = Args.Target + MemberDescriptor.InternalMemberOffset;
			MemberArgs.Prev = Args.Prev + MemberDescriptor.InternalMemberOffset;
			// Currently we pass on the changemask info unmodified to support fastarrays, but if we decide to support other serializers utilizing additional changemask we need to extend this
			MemberArgs.ChangeMaskInfo = Args.ChangeMaskInfo;

			Serializer->DeserializeDelta(Context, MemberArgs);
			if (Context.HasErrorOrOverflow())
			{
				// $IRIS TODO Provide information which member is failing to deserialize (though could be red herring)
				return;
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Quantize —— 真实 SourceType 内存 → 量化态
// -----------------------------------------------------------------------------
// 注意 offset 用法：Source 用 ExternalMemberOffset（真实内存布局），
// Target 用 InternalMemberOffset（量化态布局），二者通常不同：例如 bool
// bitfield 在 SourceType 是某字节的某位、在量化态是 uint8 整字节。
void FStructNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetQuantizeArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = Args.Source + MemberDescriptor.ExternalMemberOffset;
		MemberArgs.Target = Args.Target + MemberDescriptor.InternalMemberOffset;

		// Currently we pass on the changemask info unmodified to support fastarrays, but if we decide to support other serializers utilizing additional changemask we need to extend this
		MemberArgs.ChangeMaskInfo = Args.ChangeMaskInfo;

		Serializer->Quantize(Context, MemberArgs);
	}
}

// -----------------------------------------------------------------------------
// Dequantize —— 量化态 → 真实 SourceType 内存
// -----------------------------------------------------------------------------
void FStructNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

		FNetDequantizeArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = Args.Source + MemberDescriptor.InternalMemberOffset;
		MemberArgs.Target = Args.Target + MemberDescriptor.ExternalMemberOffset;

		Serializer->Dequantize(Context, MemberArgs);
		if (Context.HasError())
		{
			// $IRIS TODO Provide information which member is failing to dequantize
			return;
		}
	}
}

// -----------------------------------------------------------------------------
// IsEqual —— 量化/未量化态分支比较
// -----------------------------------------------------------------------------
// 量化态 + 无 dynamic state：直接 memcmp 整段 InternalSize 字节，O(InternalSize)。
// 否则逐成员调用其 Serializer 的 IsEqual。
bool FStructNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	
	// Optimized case for quantized state without dynamic state
	if (Args.bStateIsQuantized && !EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		return !FPlatformMemory::Memcmp(reinterpret_cast<const void*>(Args.Source0), reinterpret_cast<const void*>(Args.Source1), Descriptor->InternalSize);
	}

	// Per member equality
	{
		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		FNetIsEqualArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.bStateIsQuantized = Args.bStateIsQuantized;

		const bool bIsQuantized = Args.bStateIsQuantized;
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
			const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;

			MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			const uint32 MemberOffset = (bIsQuantized ? MemberDescriptor.InternalMemberOffset : MemberDescriptor.ExternalMemberOffset);
			MemberArgs.Source0 = Args.Source0 + MemberOffset;
			MemberArgs.Source1 = Args.Source1 + MemberOffset;

			if (!Serializer->IsEqual(Context, MemberArgs))
			{
				return false;
			}
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Validate —— 逐成员 Validate（仅在真实 SourceType 上做，如 NaN/范围检查）
// -----------------------------------------------------------------------------
bool FStructNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
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
		MemberArgs.Source = Args.Source + MemberDescriptor.ExternalMemberOffset;

		if (!Serializer->Validate(Context, MemberArgs))
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// CloneDynamicState —— 仅当 struct 具有 dynamic state 时才需要
// -----------------------------------------------------------------------------
// 通常发生在：基线快照 / Send queue / DC baseline 备份等需要"独立拷贝"
// 量化态的场景。普通 memcpy 不够，因为成员里可能有 TArray、Polymorphic 之类
// 的指针/堆分配。MemberTraits 上必须含 HasDynamicState 才会进入子流程。
void FStructNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	// If no member has dynamic state then there's nothing for us to do
	if (!EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		return;
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberTraitsDescriptor& MemberTraitsDescriptor = MemberTraitsDescriptors[MemberIt];
		if (!EnumHasAnyFlags(MemberTraitsDescriptor.Traits, EReplicationStateMemberTraits::HasDynamicState))
		{
			continue;
		}

		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];

		FNetCloneDynamicStateArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = Args.Source + MemberDescriptor.InternalMemberOffset;
		MemberArgs.Target = Args.Target + MemberDescriptor.InternalMemberOffset;

		Serializer->CloneDynamicState(Context, MemberArgs);
	}
}

// -----------------------------------------------------------------------------
// FreeDynamicState —— 释放 dynamic state（仅遍历有动态状态的成员）
// -----------------------------------------------------------------------------
void FStructNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	// If no member has dynamic state then there's nothing for us to do
	if (!EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		return;
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberTraitsDescriptor& MemberTraitsDescriptor = MemberTraitsDescriptors[MemberIt];
		if (!EnumHasAnyFlags(MemberTraitsDescriptor.Traits, EReplicationStateMemberTraits::HasDynamicState))
		{
			continue;
		}

		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];

		FNetFreeDynamicStateArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = Args.Source + MemberDescriptor.InternalMemberOffset;

		Serializer->FreeDynamicState(Context, MemberArgs);
	}
}

// -----------------------------------------------------------------------------
// CollectNetReferences —— 委托给 ReplicationStateOperations 的内部实现
// -----------------------------------------------------------------------------
// 这里没有自己再走一遍循环，而是直接转发给
// FReplicationStateOperationsInternal::CollectReferences —— 因为对象引用
// 的扫描需要 access 内部 ChangeMask + 多种 trait（HasObjectReference /
// HasConditionalChangeMask 等），ReplicationStateOperations 已经是一份
// 经过优化的实现，直接复用即可。
void FStructNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	// User implemented forwarding serializers don't have access to ReplicationStateOperationsInternal	
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FReplicationStateDescriptor* Descriptor = Config->StateDescriptor;
	Private::FReplicationStateOperationsInternal::CollectReferences(Context, *reinterpret_cast<FNetReferenceCollector*>(Args.Collector), Args.ChangeMaskInfo, reinterpret_cast<const uint8*>(Args.Source), Descriptor);
}

// -----------------------------------------------------------------------------
// CloneStructMember —— 用于 DeserializeDelta 短路场景的"struct 成员浅拷贝"
// -----------------------------------------------------------------------------
// 流程：(1) Free 旧 dynamic state（如有） → (2) memcpy 整个量化态 →
// (3) 对 dynamic state 调用 CloneDynamicState 重新分配/递归克隆。
// 等价于把"两个量化态实例"做一次深拷贝。
void FStructNetSerializer::CloneStructMember(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& CloneArgs)
{
	const FReplicationStateDescriptor* StateDescriptor = static_cast<const ConfigType*>(CloneArgs.NetSerializerConfig)->StateDescriptor;
	// 仅 struct 含 dynamic state 时才需要先 Free 再 Clone；否则一次 memcpy 即可。
	const bool bNeedFreeingAndCloning = EnumHasAnyFlags(StateDescriptor->Traits, EReplicationStateTraits::HasDynamicState);

	if (bNeedFreeingAndCloning)
	{
		// 先释放 Target 旧 dynamic state，避免后续 memcpy 后泄漏。
		FNetFreeDynamicStateArgs FreeArgs;
		FreeArgs.NetSerializerConfig = CloneArgs.NetSerializerConfig;
		FreeArgs.Version = CloneArgs.Version;
		FreeArgs.Source = CloneArgs.Target;
		FreeDynamicState(Context, FreeArgs);
	}

	// 整段量化态字节复制。
	FPlatformMemory::Memcpy(reinterpret_cast<void*>(CloneArgs.Target), reinterpret_cast<const void*>(CloneArgs.Source), StateDescriptor->InternalSize);

	if (bNeedFreeingAndCloning)
	{
		// 重新克隆 dynamic state（CloneDynamicState 会重新分配并递归 Clone）。
		CloneDynamicState(Context, CloneArgs);
	}
}

}
