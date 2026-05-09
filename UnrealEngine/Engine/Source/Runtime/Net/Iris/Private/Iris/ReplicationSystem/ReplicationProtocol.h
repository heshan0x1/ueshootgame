// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationProtocol.h —— Iris 复制协议核心数据结构（不可变形态描述 + 实例侧表）
// -----------------------------------------------------------------------------
// 本文件定义两类协议结构（per-class 形态 vs per-instance 表）：
//   1) FReplicationProtocol         —— 一类对象（同 Class+SubObject 组合）共享的"形态"
//      • 不可变、可在多连接间共享、由 RefCount 管理生命周期；
//      • 持有 ReplicationStateDescriptors[]（每个 Fragment 一份 Descriptor）；
//      • 缓存 InternalTotalSize/Alignment（量化后内部缓冲布局）、ChangeMaskBitCount、
//        ConditionalChangeMask 偏移、ProtocolTraits（HasDynamicState / HasObjectReference /
//        HasLifetimeConditionals / HasConditionalChangeMask / SupportsDeltaCompression /
//        HasPushBasedDirtiness 等）、ProtocolIdentifier（基于 fragments hash 计算）；
//      • PushModelOwnerRepIndexToFragmentIndexTable —— 加速 PushModel 标脏 RepIndex→Fragment 映射；
//      • TypeStatsIndex —— 与 Stats 模块协作，按"类型"维度做时间/字节数采样。
//   2) FReplicationInstanceProtocol —— 每个具体网络对象实例一份
//      • FragmentData[] 中 ExternalSrcBuffer 直接指向真实 UObject 的属性内存（Poll/Copy 入口）；
//      • Fragments[] 持有抽象 FReplicationFragment*（决定如何 Poll/Apply）；
//      • InstanceTraits（NeedsPoll / NeedsPreSendUpdate / IsBound / HasPushBasedDirtiness 等）
//        在 FReplicationProtocolManager::CreateInstanceProtocol 时由 Fragment 累积/共有 traits 计算得出。
// 与 ReplicationSystem.md §4 "数据标识、协议、工厂" 对应。
// =============================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Misc/EnumClassFlags.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"

namespace UE::Net
{
	struct FNetDebugName;
	// 协议标识符：基于所有 fragment 的 DescriptorIdentifier+DefaultStateHash 做 CityHash32 得到，
	// 用于跨连接（甚至 Server/Client）唯一识别一份"形态"，是 Protocol 缓存复用的 key。
	typedef uint32 FReplicationProtocolIdentifier;
	class FReplicationFragment;
	struct FReplicationStateDescriptor;
}

namespace UE::Net
{

// 实例协议位掩码：从所有 Fragment 的 EReplicationFragmentTraits 汇总得到的"实例级"特征。
// 用来在帧循环各阶段快速跳过不需要的处理（例如 NeedsPoll=0 的对象就不参与 Poll 阶段）。
enum class EReplicationInstanceProtocolTraits : uint16
{
	None = 0,
	NeedsPoll					= 1,                        // 需要在 Poll 阶段轮询（拉模式或"满推"以外的 Fragment）
	NeedsLegacyCallbacks		= NeedsPoll << 1,           // 需要触发旧的 RepNotify / OnRep 等回调
	IsBound						= NeedsLegacyCallbacks << 1,// 已绑定到具体 UObject 实例
	NeedsPreSendUpdate			= IsBound << 1,             // PreSendUpdate 阶段需要回调（自定义同步前钩子）
	HasPushBasedDirtiness		= NeedsPreSendUpdate << 1,  // 至少一个 Fragment 启用了 PushModel
	HasFullPushBasedDirtiness	= HasPushBasedDirtiness << 1, // 所有 Fragment 都启用 PushModel（可彻底跳过 Poll）
	// Whether this instance contains fragments from multiple owners
	// 跨多个 UObject 的多 Owner 实例：当前不支持 PushModel
	IsMultiObjectInstance	 = HasFullPushBasedDirtiness << 1,
	// Whether there's a state that has object references
	// 包含 UObject 引用 —— 触发 NetExports / ObjectReferenceCache 解引路径
	HasObjectReference = IsMultiObjectInstance << 1,
};
ENUM_CLASS_FLAGS(EReplicationInstanceProtocolTraits);

/** 
 * The ReplicationInstanceProtocol stores everything we need to know in order to interact with data outside the replication system
 * This information is only used when copying state data to replication system or when pushing state data from the replication system
 * 
 * 【实例协议】每个具体网络对象一份。是 Iris 与"游戏世界 UObject"的桥头堡：
 *  - FragmentData[i].ExternalSrcBuffer 指向真实 UObject 的属性首地址（Poll/Copy 时把外部数据拷到内部 buffer）；
 *  - Fragments[i] 是一份策略接口（基于反射的 PropertyReplicationFragment、FastArrayReplicationFragment 等）；
 *  - InstanceTraits 是从所有 Fragment 累积/共有 traits 派生的位图；
 *  - 创建/销毁集中在 FReplicationProtocolManager::CreateInstanceProtocol/DestroyInstanceProtocol，
 *    其中带 EReplicationFragmentTraits::DeleteWithInstanceProtocol 标记的 Fragment 会随实例销毁释放。
 */ 
struct FReplicationInstanceProtocol
{
	// Cached information, A fragment can register itself multiple times with different ExternalSrcBuffers to support multi state fragments
	// 一个 Fragment 可以注册多次（不同 ExternalSrcBuffer 指向不同 RepState 内存块），故 FragmentData/Fragments 是数组关系。
	struct FFragmentData
	{
		uint8* ExternalSrcBuffer;            // UObject 属性内存首地址（外部缓冲）
		EReplicationFragmentTraits Traits;   // 该 fragment 自身的 traits（缓存以避免回查 Fragment->GetTraits()）
	};
	
	FFragmentData* FragmentData;             // 长度为 FragmentCount，单次分配（与本结构连续）
	FReplicationFragment* const * Fragments; // 长度为 FragmentCount，单次分配（与本结构连续）
	uint16 FragmentCount;
	EReplicationInstanceProtocolTraits InstanceTraits;
};

// 协议级（共享形态）位掩码：决定 Iris 内部对该协议执行哪些处理路径。
// 由 ReplicationStateDescriptor 的 traits 在 CreateReplicationProtocol 时合并产生。
enum class EReplicationProtocolTraits : uint16
{
	None = 0,
	// Whether any of the replication states has dynamic state
	// 任一 state 含动态分配（TArray/FString 等），析构时需调用对应 Serializer 的 FreeDynamicState
	HasDynamicState = 1U << 0U,
	// Whether there's a state with legacy lifetime conditionals
	// 含老式 LifetimeCondition（COND_OwnerOnly 等），需要按连接重算 conditional ChangeMask
	HasLifetimeConditionals = HasDynamicState << 1U,
	// Whether there's a changemask for conditionals stored in the internal state, such as when there are custom conditionals
	// InternalState 末尾追加 conditional ChangeMask 区域（自定义条件需要持久化），偏移由 InternalChangeMasksOffset 给出
	HasConditionalChangeMask = HasLifetimeConditionals << 1U,
	// Whether there's a state that has connection specific serialization
	// 含按连接定制序列化的 state（例如 NetSerialize 内部读取 PackageMap 状态）
	HasConnectionSpecificSerialization = HasConditionalChangeMask << 1U,
	// Whether there's a state that has object references
	// 含 UObject 引用 —— 序列化时需走 ObjectReferenceCache 导出/解析路径
	HasObjectReference = HasConnectionSpecificSerialization << 1U,
	// Whether delta compression is supported or not, essentially whether it makes sense to create baselines or not.
	// 支持差分压缩 —— DeltaCompressionBaselineManager 会为该协议创建 baseline
	SupportsDeltaCompression = HasObjectReference << 1U,
	// If there are some states in the protocol that partially uses pushbased dirtiness
	// 部分 state 启用了 PushModel
	HasPushBasedDirtiness = SupportsDeltaCompression << 1U,
	// If all states uses full pushbased dirtiness
	// 全部 state 启用 PushModel —— 可完全跳过 Poll 阶段
	HasFullPushBasedDirtiness	 = HasPushBasedDirtiness << 1U,
};
ENUM_CLASS_FLAGS(EReplicationProtocolTraits);

/**
 * The Replication protocols contains everything required to express the state of a replicated object.
 * This is shared for every instance of the same type. This is used for all internal operations on state data.
 * 
 * 【共享协议形态】不可变，引用计数（多 NetObject 共用同一份），关键字段：
 *  - ReplicationStateDescriptors[]    每个 fragment 一份 Descriptor（描述每个 RepState 的成员/Serializer/ChangeMask）
 *  - InternalTotalSize / Alignment    "内部量化缓冲"的总尺寸/对齐（Iris 内统一以量化形式存放）
 *  - MaxExternalStateSize / Alignment 反量化时临时 external 缓冲的最大需求
 *  - ChangeMaskBitCount               所有 state ChangeMask 之和 + 可能追加的 conditional ChangeMask
 *  - InternalChangeMasksOffset        当 HasConditionalChangeMask 时，conditional changemask 在 InternalBuffer 中的偏移（字节）
 *  - First/LifetimeConditionalsState* 第一个 LifetimeConditional state 的索引/changemask offset，用于条件位图重算
 *  - ProtocolIdentifier               全局唯一 hash（CityHash32 fragments）—— 是缓存复用 key，Server/Client 必须一致
 *  - DebugName                        持久化字符串，可用于日志/Trace
 *  - TypeStatsIndex                   Stats 模块按类型分桶的下标（默认 -1 → DefaultTypeStatsIndex）
 *  - PushModelOwnerRepIndexToFragmentIndexTable / PushModelOwnerCount
 *                                     PushModel 路径上"按 owner+RepIndex 反查 Fragment"的加速表
 *  - RefCount                         多 NetObject 共享，归零后真正释放（FReplicationProtocolManager 维护）
 */
struct FReplicationProtocol
{
	// PushModel：每个 owner 维护一张 RepIndex → FragmentIndex 的稀疏表。
	struct FRepIndexToFragmentIndex
	{
		enum : uint16
		{
			InvalidEntry = 65535U, // 该 RepIndex 没有对应的 Fragment（属性可能不参与同步或被裁剪）
		};
		
		uint16 FragmentIndex = InvalidEntry;
	};
	struct FRepIndexToFragmentIndexTable
	{
		const FRepIndexToFragmentIndex* RepIndexToFragmentIndex; // RepIndexCount 大小的数组
		uint32 NumEntries;                                       // RepIndexCount
	};

	// Conditional ChangeMask 区域在 InternalBuffer 中的字节偏移（与 InternalChangeMasksOffset 同义，向外暴露的 getter）。
	uint32 GetConditionalChangeMaskOffset() const { return InternalChangeMasksOffset; }

	// RefCounting used to track usage of the replication protocol
	// AddRef/Release 必须成对调用；FReplicationProtocolManager 在引用归零后才真正销毁。
	IRISCORE_API void AddRef() const;
	IRISCORE_API void Release() const;
	int32 GetRefCount() const { return RefCount; }

	const FReplicationStateDescriptor** ReplicationStateDescriptors;
	uint32 ReplicationStateCount;		// Number of states  Fragment 数量
	uint32 InternalTotalSize;			// Total memory required to store the complete state  量化后内部缓冲总字节数
	uint32 InternalTotalAlignment;		// Alignment of the internal state                    内部缓冲对齐要求
	uint32 MaxExternalStateSize;		// Max external state size, required when we push temporary states to game  反量化临时缓冲最大字节
	uint32 MaxExternalStateAlignment;	// Max external state alignment, required when allocating temporary state buffer

	// These two members are only valid if traits include HasConditionalChangeMask. The target state has the HasLifetimeConditionals trait.
	// 仅当 HasConditionalChangeMask 时有效：用于在 ConditionalsUpdate 阶段做精确范围处理。
	uint16 FirstLifetimeConditionalsStateIndex;
	uint16 LifetimeConditionalsStateCount;
	uint32 FirstLifetimeConditionalsChangeMaskOffset; // ChangeMask bit offset（不是字节）

	uint32 ChangeMaskBitCount;			// How many bits do we need to store the full changemask  整个对象 changemask 总位数
	uint32 InternalChangeMasksOffset;   // 字节偏移：InternalBuffer 末尾追加的 conditional changemask 起点

	FReplicationProtocolIdentifier ProtocolIdentifier;  // 协议唯一 ID（hash），跨端必须一致
	const FNetDebugName* DebugName;                     // 持久化字符串，调试用

	// TypeStatsIndex assigned to this protocol
	// Stats 模块的"类型"分桶下标。-1 表示使用 FNetTypeStats::DefaultTypeStatsIndex。
	int32 TypeStatsIndex;

	// TODO: Cache parts of the descriptors in the protocol to avoid having to pull in the descriptor itself for operations iterating over the protocol

	// Selected traits from the ReplicationStateDescriptors that might be handy to cache in the protocol.
	// 协议级 traits（合并所有 fragment/state 后的快照），决定走哪些处理路径。
	EReplicationProtocolTraits ProtocolTraits;

	// Keep a table of repindex -> fragment for each owner with pushbased data.
	// PushModel 加速表：每个 owner 一张 RepIndex→FragmentIndex（多 owner 由 NetObjectGroups 处理）。
	const FRepIndexToFragmentIndexTable* PushModelOwnerRepIndexToFragmentIndexTable;
	// Owner count
	uint16 PushModelOwnerCount;

	mutable int32 RefCount; // 引用计数（mutable 允许 const 持有者 AddRef/Release）
};

}
