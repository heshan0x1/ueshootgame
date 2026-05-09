// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationOperations.h —— Iris "状态级 / 对象级 / 协议级" 操作公共门面
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外 API（含 IRISCORE_API 标记）。
// 角色：把 Iris 在三个粒度上对状态做的标准处理动作集中暴露成静态门面，按层次依次：
//   1) FReplicationStateOperations   —— 单个 ReplicationState（对应一个 FReplicationStateDescriptor）粒度的
//        Quantize / Dequantize / Validate / Serialize / Apply / IsEqual / FreeDynamicState 等。
//        所有操作要么"全成员"，要么"按 ChangeMask 按需"。
//   2) FReplicationInstanceOperations —— 一个 NetObject 的 Instance 粒度（即一组 fragment）：
//        PollAndCopy / Quantize（写出 ChangeMask）/ ResetDirtiness / DequantizeAndApply 等。
//        Quantize 既可"全量" Quantize，也可"仅 dirty" QuantizeIfDirty。
//   3) FReplicationProtocolOperations —— 整个对象的状态缓冲（按 Protocol 形态）粒度：
//        Serialize/Deserialize（带 ChangeMask 序列化、初始状态 delta、与上一帧 baseline 的 delta）、
//        IsEqualQuantizedState / FreeDynamicState / InitializeFromDefaultState 等。
//
// 数据流走向（与 Iris_Architecture.md §3.7-§3.8 对应）：
//   外部缓冲（UObject 内的 UProperty 内存布局）
//     ─── Quantize ───►  内部缓冲（量化后 Iris 紧凑表示，定界对齐）
//     ◄── Dequantize ──
//     ─── Serialize ──►  BitStream（含 ChangeMask + 成员数据）
//     ◄── Deserialize ─
//   ChangeMask 用于减少传输：仅成员被标脏时才进入序列化流。
//
// 调用层级（顶向下）：
//   UReplicationSystem -> FReplicationSystemImpl -> FReplicationOperations(此处)
//                                              -> FReplicationInstanceOperationsInternal（仅内部）
//   Reader/Writer 在序列化阶段调用 FReplicationProtocolOperations / FReplicationStateOperations。
//
// 注：很多函数签名需要用户提前保证 buffer 的 ALIGN 与构造状态正确（Quantize 写入 Internal 时不要求初始化；
// Dequantize 写入 External 时要求已构造）。
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"

class FMemStackBase;
namespace UE::Net
{
	class FNetBitStreamWriter;
	class FNetBitStreamReader;
	class FNetSerializationContext;
	struct FReplicationInstanceProtocol;
	struct FReplicationStateDescriptor;
	class FReplicationStateOwnerCollector;
	struct FReplicationProtocol;
	struct FDequantizeAndApplyParameters;
}

namespace UE::Net
{

//$IRIS TODO: Consider what methods we should expose here, currently they are all public!

// FReplicationStateOperations ── 单个 ReplicationState（一个 FReplicationStateDescriptor）粒度的操作门面
struct FReplicationStateOperations
{
	/** Quantize a Replication state from ExternalBuffer to internal buffer, DstInternalBuffer does not need to be initialized */
	// 把"用户态"状态量化进"Iris 内部紧凑表示"——逐成员调用 Serializer->Quantize。
	// DstInternalBuffer 无需事先构造（Quantize 自身写入即定义内存）。
	static IRISCORE_API void Quantize(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Dequantize a Replication state from internal buffer to already constructed ExternalBuffer */
	// 全量 Dequantize：把内部表示展回外部表示（要求 DstExternalBuffer 已构造完毕，因为某些类型有析构语义）。
	static IRISCORE_API void Dequantize(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Dequantize a Replication state from internal buffer to already constructed ExternalBuffer */
	// 按 ChangeMask 选择性 Dequantize：仅展开 ChangeMask 中 [ChangeMaskOffset, ChangeMaskOffset+BitCount] 范围内被标脏的成员。
	// 用于 Apply 路径只回写真正变化的字段，避免覆盖客户端尚未脏传到的本地修改。
	static IRISCORE_API void DequantizeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Quantize a Replication state from ExternalBuffer to internal buffer, DstInternalBuffer does not need to be initialized */
	// 按 ChangeMask 选择性 Quantize：与 DequantizeWithMask 对称。仅"非 init state"使用。
	static IRISCORE_API void QuantizeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Validate a ReplicationState in external format */
	// 调试 / 安全检查：逐成员调用 Serializer->Validate；任一返回 false 即视为非法。
	static IRISCORE_API bool Validate(FNetSerializationContext& Context, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** FreeDynamicState free all dynamic memory allocated for quantized state data */
	// 释放量化后状态所持有的动态内存（如 dynamic array / 字符串内部 buffer）。
	// 仅对带 EReplicationStateTraits::HasDynamicState 的 Descriptor 才有事可做。
	static IRISCORE_API void FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT StateInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Compare two quantized states return false if they are different */
	// 两份内部量化状态等值比较（逐成员调用 Serializer->IsEqual，bStateIsQuantized=true）。
	static IRISCORE_API bool IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT Source0, const uint8* RESTRICT Source1, const FReplicationStateDescriptor* Descriptor);

	/** Debug method to output the per member defaultstate hash */
	// 调试方法：打印每个成员"默认状态"的序列化 hash，用于诊断协议不一致 / 默认值差异。
	static IRISCORE_API void OutputDefaultStateMembersHashToString(UReplicationSystem* ReplicationSystem, FStringBuilderBase& StringBuilder, const FReplicationStateDescriptor* Descriptor);

	/** Serialize a Replication state from internal buffer to BitStream */
	// 序列化（无 mask）：逐成员写入 BitStream。
	static IRISCORE_API void Serialize(FNetSerializationContext& Context, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Deserialize Replication state from BitStream to internal buffer */
	// 反序列化（无 mask）：逐成员从 BitStream 读出到内部缓冲。
	static IRISCORE_API void Deserialize(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Serialize a Replication state from internal buffer to BitStream */
	// Delta 序列化：相对 PrevInternalBuffer（基线）做差量编码（每个 Serializer 自定义如何 delta）。
	static IRISCORE_API void SerializeDelta(FNetSerializationContext& Context, const uint8* RESTRICT SrcInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Deserialize Replication state from BitStream to internal buffer */
	// Delta 反序列化：与 SerializeDelta 对称。
	static IRISCORE_API void DeserializeDelta(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Serialize a Replication state from internal buffer to BitStream */
	// 带 Mask 的序列化：仅成员 ChangeMask bit 为 1 的成员才写入 BitStream（节省带宽核心路径）。
	static IRISCORE_API void SerializeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Deserialize Replication state from BitStream to internal buffer */
	// 带 Mask 的反序列化：与 SerializeWithMask 对称。Reader 必须先解析出 ChangeMask 再调用。
	static IRISCORE_API void DeserializeWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Serialize a Replication state from internal buffer to BitStream */
	// 带 Mask 的 Delta 序列化：把 SerializeDelta 与 SerializeWithMask 合并（DeltaCompression 子模块路径用）。
	static IRISCORE_API void SerializeDeltaWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Deserialize Replication state from BitStream to internal buffer */
	// 带 Mask 的 Delta 反序列化：与 SerializeDeltaWithMask 对称。
	static IRISCORE_API void DeserializeDeltaWithMask(FNetSerializationContext& Context, const FNetBitArrayView& ChangeMask, const uint32 ChangeMaskOffset, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT PrevInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Apply/copy state for a descriptor created for a struct. */
	// 仅对 Struct 描述符（不属于完整 NetObject）使用：把 SrcExternalBuffer 的内容应用到 DstExternalBuffer。
	// 内部走 InternalApplyStructProperty —— 仅复制"被复制成员"，保留本地非复制字段。
	static IRISCORE_API void ApplyStruct(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcExternalBuffer, const FReplicationStateDescriptor* Descriptor);
};

// FReplicationInstanceOperations ── NetObject "实例" 粒度（一组 Fragment）操作门面
struct FReplicationInstanceOperations
{
	/** Update all registered Fragments that updates dirtiness by polling, except for those with any of the ExcludeTraits. Returns true if a polled state is dirty. */
	// 遍历 InstanceProtocol 上所有 NeedsPoll 的 Fragment 调用 PollReplicatedState。
	// ExcludeTraits 用于排除某些子集（例如本帧只 poll 普通属性，不 poll 含引用的）。
	// 返回是否至少有一个状态变脏（用于决定是否继续走 Quantize）。
	static IRISCORE_API bool PollAndCopyPropertyData(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentTraits ExcludeTraits, EReplicationFragmentPollFlags PollOptions = EReplicationFragmentPollFlags::PollAllState);

	/** Update all registered Fragments that updates dirtiness by polling.  Returns true if a polled state is dirty. */
	// 不带 Exclude 的便利重载（Exclude=None）。
	static IRISCORE_API bool PollAndCopyPropertyData(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentPollFlags PollOptions = EReplicationFragmentPollFlags::PollAllState);

	/** Update object references in fragments that has object references and additional required traits. Returns true if a polled state is dirty. */
	// 仅刷新对象引用（GC 后等场景）：只 poll 同时具备 HasObjectReference + RequiredTraits 的 Fragment。
	// 自动追加 ForceRefreshCachedObjectReferencesAfterGC 标志。
	static IRISCORE_API bool PollAndCopyObjectReferences(const FReplicationInstanceProtocol* InstanceProtocol, EReplicationFragmentTraits RequiredTraits, EReplicationFragmentPollFlags PollOptions = EReplicationFragmentPollFlags::None);

	/**
	 * Quantize the state for a replicated object with a given InstanceProtocol using the ReplicationProtocol. 
	 * DstObjectStateBuffer needs to be in a valid state before calling this function.
	 * Changemasks will be written to the ChangeMaskWriter. Dirtiness will not be reset.
	 * @see ResetDirtiness
	 */
	// 全量 Quantize 单个对象（所有 Fragment 全部走一次 Quantize），把每个状态的 ChangeMask 拼接写入 ChangeMaskWriter。
	// 注意：调用本函数后 dirty 标记仍保留，需要单独调用 ResetDirtiness 来清除。
	static IRISCORE_API void Quantize(FNetSerializationContext& Context, uint8* DstObjectStateBuffer, FNetBitStreamWriter* ChangeMaskWriter, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/**
	 * Quantize the state for a replicated object with a given InstanceProtocol using the ReplicationProtocol.
	 * DstObjectStateBuffer needs to be in a valid state before calling this function.
	 * Changemasks will be written to the ChangeMaskWriter. Dirtiness will not be reset.
	 * This variant will only Quantize States marked as dirty
	 * @see ResetDirtiness
	 */
	// 仅 Quantize 标脏状态（生产路径默认）。比 Quantize 节省大量 CPU——多数对象大多数帧不脏。
	// init state 总是全量 Quantize；非 init state 在 cvar net.Iris.OnlyQuantizeDirtyMembers=true 时仅 Quantize 脏成员。
	static IRISCORE_API void QuantizeIfDirty(FNetSerializationContext& Context, uint8* DstObjectStateBuffer, FNetBitStreamWriter* ChangeMaskWriter, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/** Resets dirty tracking stored with the protocol, such as changemasks and init state dirtiness. */
	// 重置该实例的全部 dirty 标记（成员 ChangeMask + init state dirtiness + Poll mask）。
	// 通常在 Quantize 完成、并把 ChangeMask 提交给 Writer 后调用。
	static IRISCORE_API void ResetDirtiness(const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/** Dequantize the state for a replicated object with a given protocol. Data will be pushed out by using the ReplicationFragments. */
	// 客户端接收路径：把内部量化状态解出 → 调用各 Fragment 的 ApplyReplicatedState 与（可选）CallRepNotifies。
	// 实际由 FDequantizeAndApplyHelper 完成（位于 Private）。
	static IRISCORE_API void DequantizeAndApply(FNetSerializationContext& Context, const FDequantizeAndApplyParameters& Parameters);

	/** Dequantize the state for a replicated object with a given protocol. Data will be pushed out by using the ReplicationFragments. */
	// 等价便利重载：把多个参数包装成 FDequantizeAndApplyParameters。
	static IRISCORE_API void DequantizeAndApply(FNetSerializationContext& Context, FMemStackBase& InAllocator, const uint32* ChangeMaskData, const FReplicationInstanceProtocol* InstanceProtocol, const uint8* SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Dequantize the state for a replicated object with a given protocol and output the state to string. */
	// 调试输出：把内部状态展开（按 Fragment 形态）后用 ReplicatedStateToString 输出，便于 RepDebugger 检查。
	static IRISCORE_API void OutputInternalStateToString(FNetSerializationContext& Context, FStringBuilderBase& StringBuilder, const uint32* ChangeMaskData, const uint8* SrcInternalObjectStateBuffer, const FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/** Dequantize the default state for a replicated object with a given protocol and output the state to string. */
	// 调试输出对象的默认状态（每个 Descriptor 的 DefaultStateBuffer），用于核对协议不一致。
	static IRISCORE_API void OutputInternalDefaultStateToString(FNetSerializationContext& NetSerializationContext, FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments);

	/** Serialize and output per member default state hashes to string. */
	// 调试输出：按成员序列化默认状态并 hash，便于"为什么协议不匹配"的诊断。
	static IRISCORE_API void OutputInternalDefaultStateMemberHashesToString(UReplicationSystem* ReplicationSystem, FStringBuilderBase& StringBuilder, const FReplicationFragments& Fragments);

};

// FReplicationProtocolOperations ── 整个对象的 Protocol（多个 Descriptor 拼接）粒度操作门面
struct FReplicationProtocolOperations
{
	/** Serialize the state for a full NetObject to BitStream */
	// 全量序列化整个对象的所有 ReplicationState（按 Descriptor 列表逐个 Serialize）。
	static IRISCORE_API void Serialize(FNetSerializationContext& Context, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Deserialize the state of a NetObject from a BitStream to a ObjectStateBuffer large enough to fit all data */
	// 全量反序列化对象到 DstObjectStateBuffer（必须足够大；上层按 Protocol->InternalTotalSize 分配）。
	static IRISCORE_API void Deserialize(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Serialize the state of all dirty members a NetObject and changemask to Bitstream */
	// 带 ChangeMask 的对象级序列化：先 WriteSparseBitArray 写出 ChangeMask，再逐 Descriptor 调用 SerializeWithMask；
	// init state 仅在 Context.IsInitState() 为 true 时写出。
	static IRISCORE_API void SerializeWithMask(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Deserialize changemask and the state of an object from a BitStream to a ObjectStateBuffer large enough to fit all data */
	// 带 ChangeMask 的对象级反序列化（与 SerializeWithMask 对称）。
	static IRISCORE_API void DeserializeWithMask(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Compare two quantized states and return whether they're equal or not. */
	// 对象级量化状态比较；某个 Descriptor 不等即返回 false。
	static IRISCORE_API bool IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT Source0, const uint8* RESTRICT Source1, const FReplicationProtocol* Protocol);

	/** Free dynamic state for the entire protocol. */
	// 对象级释放动态状态；仅当 Protocol 含 HasDynamicState trait 才需要做事。
	static IRISCORE_API void FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Initialize from default state */
	// 用 Descriptor 默认状态初始化对象状态缓冲（典型用于"接收端创建新对象"的初始化路径，缺成员不发，用默认值占位）。
	static IRISCORE_API void InitializeFromDefaultState(FNetSerializationContext& Context, uint8* RESTRICT StateBuffer, const FReplicationProtocol* Protocol);

	/** Serialize the initial state of all dirty members a NetObject and changemask to a Bitstream, delta compressed against the default state. */
	// 初始状态的特殊路径：对默认状态做 delta（默认值不发，仅发与默认不同的部分）。受 cvar net.Iris.DeltaCompressInitialState 控制。
	static IRISCORE_API void SerializeInitialStateWithMask(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Deserialize changemask and the state of an object from a BitStream to an ObjectStateBuffer large enough to fit all data delta compressed against default state. */
	// 与 SerializeInitialStateWithMask 对称的反序列化。Reader 端先 CloneQuantizedState(默认 → 目标)，再 DeserializeDelta。
	static IRISCORE_API void DeserializeInitialStateWithMask(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Serialize the state of all dirty members of an object and changemask to a Bitstream, delta compressed against the PrevObjectStateBuffer. */
	// DeltaCompression 子模块路径：相对一个具体 baseline（PrevObjectStateBuffer）做 delta，不是相对默认。
	static IRISCORE_API void SerializeWithMaskDelta(FNetSerializationContext& Context, const uint32* ChangeMaskData, const uint8* RESTRICT SrcObjectStateBuffer, const uint8* RESTRICT PrevObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Deserialize changemask and the state of an object from a BitStream to an ObjectStateBuffer large enough to fit all data, delta compressed against the PrevObjectStateBuffer. */
	// 与 SerializeWithMaskDelta 对称的反序列化。
	static IRISCORE_API void DeserializeWithMaskDelta(FNetSerializationContext& Context, uint32* DstChangeMaskData, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT PrevObjectStateBuffer, const FReplicationProtocol* Protocol);
};

// 测试 / 重置：清空 lifetime condition 调试名 ID 的全局表（NetTrace 使用）。
IRISCORE_API void ResetLifetimeConditionDebugNames();

}
