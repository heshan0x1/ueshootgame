// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationOperationsInternal.h —— "状态/对象/协议级"操作的私有实现入口
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Private/Iris/ReplicationSystem/  ← 仅 Iris 内部使用。
// 角色：与 Public 的 FReplicationOperations 门面互补，提供更"贴近内部状态"的操作集合：
//   1) FReplicationInstanceOperationsInternal —— 与 InstanceProtocol/NetHandle 紧绑：
//        * BindInstanceProtocol / UnbindInstanceProtocol —— 把 FNetHandle.Id 写入每个 ExternalSrcBuffer 的
//          FReplicationStateHeader（30-bit NetHandleId），让 GlobalDirtyNetObjectTracker 标脏时能找到对象。
//        * QuantizeObjectStateData —— 由 FReplicationSystemImpl::QuantizeDirtyStateData 调用：
//            - 取 InternalIndex 对应对象，分配 ChangeMask 槽位（FChangeMaskCache）；
//            - 选择全量 / 仅脏 quantize；
//            - 对 SubObject 自动把"父对象"标脏（保证父子原子可达）。
//        * ResetObjectStateDirtiness —— 与上面的 Quantize 配对，提交后清除 ChangeMask + Poll mask。
//
//   2) FReplicationStateOperationsInternal —— 单个 ReplicationState 的"动态状态"专项：
//        * CloneDynamicState / FreeDynamicState —— 拷贝/释放成员持有的动态内存（dyn array / string / map 等）；
//        * CloneQuantizedState —— 量化态拷贝（Memcpy + 按需 CloneDynamicState）；
//        * CollectReferences / CollectReferencesWithMask —— 把状态中的 UObject 引用收集到 NetReferenceCollector，
//            可选按 ChangeMask 过滤；用于 ReplicationWriter 在打包时确保引用先发出。
//
//   3) FReplicationProtocolOperationsInternal —— 整个对象级别的辅助：
//        * CloneDynamicState / CloneQuantizedState / FreeDynamicState
//        * CollectReferences —— 对整个对象做引用收集；非 init 状态走 CollectReferencesWithMask，init 走全量。
//        * IsEqualQuantizedState —— 整个对象量化态比较（init state 仅在 Context.IsInitState() 时参与）。
//
// 与文档对应：
//   - ReplicationSystem.md §2.6（序列化/状态拷贝路径）。
//   - Iris_Architecture.md §3.7-§3.8。
//
// 触发关系：
//   FReplicationSystemImpl::QuantizeDirtyStateData
//     ── 遍历 DirtyObjectsToQuantize 位图 ──►
//        FReplicationInstanceOperationsInternal::QuantizeObjectStateData
//             └─> FReplicationInstanceOperations::QuantizeIfDirty
//                     └─> FReplicationStateOperations::Quantize / QuantizeWithMask
// =====================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Iris/Core/NetObjectReference.h"
#include "Net/Core/NetHandle/NetHandle.h"


namespace UE::Net 
{
	class FNetBitStreamWriter;
	class FNetSerializationContext;
	struct FNetSerializerChangeMaskParam;
	struct FReplicationStateDescriptor;
	struct FReplicationInstanceProtocol;
	struct FReplicationProtocol;
	class FNetReferenceCollector;

	namespace Private
	{
		struct FChangeMaskCache;
		class FNetRefHandleManager;
	}
}

namespace UE::Net::Private
{

// FReplicationInstanceOperationsInternal —— 实例 / Handle 紧耦合的内部操作
struct FReplicationInstanceOperationsInternal
{
	/** A call to this function will inject the index of the handle into the external statebuffer contained in the Instance Protocol */
	// 把 FNetHandle 的 Id 注入到 InstanceProtocol 中每个 ExternalSrcBuffer 的 FReplicationStateHeader。
	// 这是 PushModel 标脏的关键：游戏代码只持有 UObject 指针，但 GlobalDirtyNetObjectTracker 需要 NetHandleId 才能定位脏对象。
	// 通过把 Id 嵌入 ExternalSrcBuffer 头部，标脏代码无需额外查询。
	static IRISCORE_API void BindInstanceProtocol(FNetHandle NetHandle, FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/** A call to this function will clear the index of the handle into the external statebuffer contained in the Instance Protocol */
	// 解绑：在停止复制 / 销毁对象时把 NetHandleId 清除（写入空 FNetHandle）。
	// 内部直接调用 BindInstanceProtocol(空 handle, ...)。
	static IRISCORE_API void UnbindInstanceProtocol(FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol);

	/** Quantize state data for a single instance */
	// 单对象 Quantize 入口（被 FReplicationSystemImpl::QuantizeDirtyStateData 在并行任务中调用）。
	// 流程：
	//   1) 校验对象在当前 scope 内（IsScopableIndex）；
	//   2) 在 FChangeMaskCache 中分配一段位图存放本帧 ChangeMask；
	//   3) 决定走全量 Quantize 还是 QuantizeIfDirty（受 cvar net.iris.ForceFullCopyAndQuantize / Object.bNeedsFullCopyAndQuantize 控制）；
	//   4) 若是 SubObject，把 SubObjectOwner 也加入 SubObjectOwnerDirty 列表；
	//   5) 若 bShouldPropagateChangedStates=false（如 dormant），把刚加入的 ChangeMask 槽位 PopLastEntry 撤回；
	// 返回：1 表示成功处理一个对象（有效 InternalIndex 且有内容），0 表示跳过。
	static IRISCORE_API uint32 QuantizeObjectStateData(FNetBitStreamWriter& ChangeMaskWriter, FChangeMaskCache& Cache, FNetRefHandleManager& NetRefHandleManager, FNetSerializationContext& SerializationContext, uint32 InternalIndex);

	/** Reset instance state dirtiness. */
	// 配合 QuantizeObjectStateData 使用：把刚 quantize 完的对象的 ChangeMask + Poll mask 清空，等待下一帧。
	// 调用前提是 Quantize 完成且 ChangeMask 已被 Writer 取走（不会再依赖原始位图）。
	static IRISCORE_API void ResetObjectStateDirtiness(FNetRefHandleManager& NetRefHandleManager, uint32 InternalIndex);
};

// FReplicationStateOperationsInternal —— 单个 ReplicationState 的内部辅助（动态内存 + 引用）
struct FReplicationStateOperationsInternal
{
	/** Clone the dynamic state from source to destination state for a single replication state */
	// 拷贝动态内存（dynamic array 内部 buffer 等）；调用前 DstInternalBuffer 的"标量部分"已经 memcpy 完成，
	// 此函数仅复制需要"独立分配"的部分。仅遍历 HasDynamicState 的成员。
	static IRISCORE_API void CloneDynamicState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Free the dynamic state from a state for a single replication state */
	// 释放动态内存（与 CloneDynamicState 对称）。仅遍历 HasDynamicState 的成员。
	static IRISCORE_API void FreeDynamicState(FNetSerializationContext& Context, uint8* ObjectStateBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Free the dynamic state from a state for a single replication state */
	// 便利重载：调用方没有 NetSerializationContext 时使用（内部构造一个临时 context）。
	static IRISCORE_API void FreeDynamicState(uint8* ObjectStateBuffer, const FReplicationStateDescriptor* Descriptor);

	/** Clone a quantized state, Note: DstInternalBuffer is expected to be uninitialized */
	// 量化态完整克隆 = Memcpy 标量段 + CloneDynamicState 动态段。Dst 不需要事先构造（Memcpy 会覆盖）。
	static IRISCORE_API void CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);
	
	/** Collect references from a state that does not have any changemask information */
	// 收集状态中所有 UObject 引用到 NetReferenceCollector：用于 init state（无 ChangeMask）或调用方主动想全量收集的场景。
	static IRISCORE_API void CollectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const FNetSerializerChangeMaskParam& OuterChangeMaskInfo, const uint8* RESTRICT SrcInternalBuffer,  const FReplicationStateDescriptor* Descriptor);

	/** Collect references from a state based on the provided changemask information */
	// 按 ChangeMask 选择性收集引用：仅 ChangeMask bit 为 1 的成员才参与。Writer 打包阶段使用。
	static IRISCORE_API void CollectReferencesWithMask(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer,  const FReplicationStateDescriptor* Descriptor);
};

// FReplicationProtocolOperationsInternal —— 整个对象级（多 Descriptor）的内部辅助
struct FReplicationProtocolOperationsInternal
{
	/** Clone the dynamic state from source to destination state for a full NetObject */
	// 对象级动态状态克隆：逐 Descriptor 处理对齐 + 调用 FReplicationStateOperationsInternal::CloneDynamicState。
	static IRISCORE_API void CloneDynamicState(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Clone a quantized state, Note: DstObjectStateBuffer is expected to be uninitialized */
	// 对象级量化态克隆：Memcpy(Protocol->InternalTotalSize) + 仅在 HasDynamicState 时再走 CloneDynamicState。
	// Dst 不需事先构造。
	static IRISCORE_API void CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Free the dynamic state from a state for a full NetObject */
	// 对象级释放动态状态：逐 Descriptor 调用 FreeDynamicState（仅 HasDynamicState 状态参与）。
	static IRISCORE_API void FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT ObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Collect references from protocol, if changemask is availabe in the Context it will be used */
	// 对象级引用收集；自动按 init / 非 init 选择 CollectReferences 或 CollectReferencesWithMask。
	static IRISCORE_API void CollectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol);

	/** Compare two quantized states and return false if they are different. */
	// 对象级量化态比较；非 init 状态总是参与，init 状态仅在 IsInitState() 时参与。
	static IRISCORE_API bool IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT State0, const uint8* RESTRICT State1, const FReplicationProtocol* Protocol);
};

}
