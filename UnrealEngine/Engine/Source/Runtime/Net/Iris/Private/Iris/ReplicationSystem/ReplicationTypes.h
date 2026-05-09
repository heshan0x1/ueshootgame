// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationTypes.h —— Iris ReplicationSystem 模块的"内部小型类型仓库"
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Private/Iris/ReplicationSystem/  ← 仅 Iris 内部使用。
//
// 本文件定义两类东西：
//   1) FInternalNetRefIndex  —— 整个 Iris 复制系统统一使用的"内部对象索引"类型别名（uint32）。
//      所有 NetRefHandleManager / Filtering / Prioritization / DeltaCompression 等子系统都用这个索引在
//      平直数组（FNetBitArray、TArray<FReplicatedObjectData> 等）中定位某个 NetObject。
//      它和对外公开的 FNetRefHandle（64-bit 全局唯一标识）是一对映射关系：
//         FNetRefHandle ⇄ InternalIndex（仅在当前 ReplicationSystem 实例内有效，重启会重新分配）。
//
//   2) FReplicationParameters —— Reader/Writer 在每条连接上"开机时"用于初始化的参数块。
//      由 FReplicationConnections 创建连接时填充，并传给 FReplicationWriter::Init 与 FReplicationReader::Init。
//      参数包括：内部索引上限、单帧最大对象数、滑窗大小、连接 Id、附件可见性策略、小对象阈值与位宽配置。
//
//   3) EReplicatedDestroyHeaderFlags —— Writer/Reader 之间序列化"对象停止复制/销毁/TearOff"标志位时用的紧凑位枚举。
//      只占 3 位，和 ReplicationBridgeTypes.h 中 EEndReplicationFlags 概念呼应（对外 API 用后者，二者并非同一枚举）。
//
// 与文档对应：
//   - ReplicationSystem.md §5（I/O 每连接一套）—— Writer/Reader 各持有自己的 FReplicationParameters。
//   - Iris_Architecture.md §3.8 —— 顶层协调者 + I/O 子系统介绍。
//
// 注意：本文件历史上还会承载更多内部类型；目前裁剪后只剩这三块内容。
// =====================================================================================================================

#pragma once
#include "Net/Core/NetBitArray.h"

class UReplicationSystem;

namespace UE::Net::Private
{
	// 所有 NetObject 在当前 ReplicationSystem 内部的"线性索引"。
	// 0 视为 Invalid；范围由 FReplicationParameters::MaxInternalNetRefIndex 决定。
	// 之所以单独 typedef 而非直接 uint32，是为了便于将来切换宽度（uint16/uint32/动态扩展）以及强类型审查。
	typedef uint32 FInternalNetRefIndex;
}

namespace UE::Net::Private
{

// FReplicationParameters
// 每条连接初始化 Reader / Writer 时使用的参数块。
// 由 FReplicationConnections::AddConnection 路径在 UReplicationSystem::AddConnection 中构造；
// 之后下发到 FReplicationWriter::Init 与 FReplicationReader::Init。
// 一旦初始化完成基本不再变化（除非连接重建）。
struct FReplicationParameters
{
	// 内部索引上限（独占）。等于 NetRefHandleManager 的最大对象数。Writer/Reader 用它分配 PerObjectInfo / 位图。
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;

	// 单帧 ReplicationWriter 内部允许同时跟踪的最大对象数（一般等于 MaxInternalNetRefIndex 或更小，
	// 控制 Writer 的 PerObjectInfo / 调度数组大小，避免极端情况下分配过大）。
	uint32 MaxReplicationWriterObjectCount = 0;

	// 发送窗口大小（每连接的可靠/Reliable Replication 记录条数），决定 ack 队列长度。
	uint32 PacketSendWindowSize = 0;

	// 连接 Id（NetDriver 给定，唯一标识此连接），调试/日志/筛选都会用到。
	uint32 ConnectionId = 0;

	// 反向句柄，方便 Reader/Writer 回到 ReplicationSystem 内部访问其它子系统。
	UReplicationSystem* ReplicationSystem = nullptr;

	// 是否允许"向不在该连接 scope 内的对象发送附件（Attachment / RPC）"。
	// 比如对未 scope 的对象发起 multicast RPC：默认行为可由此开关控制。
	bool bAllowSendingAttachmentsToObjectsNotInScope = false;

	// 是否允许"接收远端发来的、本地不在 scope 的对象的附件"（关闭则丢弃）。
	bool bAllowReceivingAttachmentsFromRemoteObjectsNotInScope = false;

	// 是否允许把"含未解析对象引用"的附件延迟到后续解析后再投递（关闭则立即丢弃或上抛）。
	bool bAllowDelayingAttachmentsWithUnresolvedReferences = false;

	// 当包内剩余位数不足时，仍尝试塞入"小对象"的阈值（位）。
	// 当一个对象的写入因空间不足失败后，剩余 bit 数 ≥ 此值仍会尝试更小的对象，提高带宽利用率。
	uint32 SmallObjectBitThreshold = 160U; // Number of bits remaining in a packet for us to consider trying to serialize a replicated object

	// 在第一个 stream overflow 之后，最多继续尝试多少个对象来填充包尾。
	// 用 CPU 换带宽的权衡：值越大填得越满，但耗费更多 CPU。
	uint32 MaxFailedSmallObjectCount = 10U;	// Number of objects that we try to serialize after an initial stream overflow to fill up a packet, this can improve bandwidth usage but comes at a cpu cost

	// 普通对象 batch 头部"长度字段"位宽（默认 16 位 = 最多 65535 bits / batch）。
	uint32 NumBitsUsedForBatchSize = 16U;

	// HugeObject 通道 batch 头部"长度字段"位宽（默认 32 位，支持极大对象）。
	uint32 NumBitsUsedForHugeObjectBatchSize = 32U;
};

// EReplicatedDestroyHeaderFlags
// 当 Writer 把"对象的销毁/停止复制/TearOff"信息打包给 Reader 时，会写入一个 3-bit 的 header；
// 各个 bit 的含义如下。Reader 据此决定是否继续复制最后一帧状态、是否摧毁实例、还是仅 TearOff（保留实例但不再同步）。
enum EReplicatedDestroyHeaderFlags : uint32
{
	ReplicatedDestroyHeaderFlags_None						= 0U,
	// TearOff：客户端保留对象但服务端不再继续复制——常用于 Actor 死亡掉落后仍保留尸体。
	ReplicatedDestroyHeaderFlags_TearOff					= 1U << 0U,
	// EndReplication：仅停止复制，不一定销毁本地实例（视 DestroyInstance 位决定）。
	ReplicatedDestroyHeaderFlags_EndReplication				= ReplicatedDestroyHeaderFlags_TearOff << 1U,
	// DestroyInstance：客户端应在 EndReplication 时销毁本地实例。
	ReplicatedDestroyHeaderFlags_DestroyInstance			= ReplicatedDestroyHeaderFlags_EndReplication << 1U,
	// 总位数。Writer/Reader 据此用 SerializeBits 序列化整段 header。
	ReplicatedDestroyHeaderFlags_BitCount					= 3U
};

}
