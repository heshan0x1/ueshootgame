// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/NetErrorContext.h"
#include "Iris/Serialization/NetJournal.h"

class FName;
class INetBlobReceiver;
class FNetTraceCollector;
namespace UE::Net
{
	class FNetBitArrayView;
	class FNetBitStreamReader;
	class FNetBitStreamWriter;
	class FNetTokenStore;
	class FNetTokenStoreState;

	namespace Private
	{
		class FInternalNetSerializationContext;
		class FNetExportContext;
		class FNetStatsContext;
	}
}

namespace UE::Net
{

/**
 * FNetSerializationContext —— Iris 所有 `FNetSerializer` 函数族的"共同第一参数"。
 *
 * 设计动机：
 *   Serializer 的接口签名如 `Serialize(FNetSerializationContext&, const FNetSerializeArgs&)`。
 *   Iris 内部的 Serializer 数量众多（标量/字符串/数学/对象引用/结构/数组 …），每一个
 *   Serialize/Deserialize/Quantize/... 操作都需要能够访问到：
 *     - 位流（读或写）；
 *     - 错误/溢出报告通道；
 *     - 调试日志（读 Journal、TraceCollector）；
 *     - 内部上下文（`ReplicationSystem` / `ObjectReferenceCache` / 内存分配器 / `IrisObjectReferencePackageMap`）；
 *     - 导出上下文（`FNetExportContext` 跟踪 `FNetRefHandle` / `FNetToken` / `FNetObjectReference`）；
 *     - 统计上下文（`FNetStatsContext`）；
 *     - 当前 Protocol 的 `ChangeMask`；
 *     - NetBlob 接收器（重量级 RPC / 可分片对象携带）；
 *     - 本地连接 id、当前 PacketId、初始状态/空状态标志。
 *   若每个函数都单独传这些参数，签名将极长。Iris 的做法是把所有这些"世界状态"打包进一个
 *   可在栈上构造的 `FNetSerializationContext`，函数族只暴露 `Context&`。
 *
 * 生命周期约束：
 *   - 通常在调用链入口处栈上创建（例如 `FReplicationWriter::WriteSentinel/WriteObjects` 或
 *     `FReplicationReader::ReadObjects`），在一次读/写流程内保持有效；
 *   - 不跨线程：所有子字段（位流、ExportContext、InternalContext）皆为非线程安全；
 *   - 可通过 `MakeSubContext` 切换子位流（用于 SubStream 回滚/探测写入）。
 *
 * 关键副作用：
 *   - `SetError(name, bDoOverFlow=true)` 会**同时**把底层位流标记为溢出，后续读/写均会短路；
 *   - `SetBitStreamOverflow()` 会同时让 Reader/Writer 都进入溢出状态（阻断流水线）。
 */
class FNetSerializationContext
{
public:
	/** 构造空 context（无任何位流与子上下文，仅用于后续 Set*）。 */
	FNetSerializationContext();
	/** 构造读写兼容 context——通常测试场景（实际读/写只会用到其中一个）。 */
	FNetSerializationContext(FNetBitStreamReader*, FNetBitStreamWriter*);
	/** 构造只读 context。调用方后续只能走 Deserialize/Dequantize 路径。 */
	explicit FNetSerializationContext(FNetBitStreamReader*);
	/** 构造只写 context。调用方后续只能走 Serialize/Quantize 路径。 */
	explicit FNetSerializationContext(FNetBitStreamWriter*);

	/** 获取底层比特读取器。若未绑定 Reader 将返回 nullptr。 */
	FNetBitStreamReader* GetBitStreamReader() { return BitStreamReader; }
	/** 获取底层比特写入器。若未绑定 Writer 将返回 nullptr。 */
	FNetBitStreamWriter* GetBitStreamWriter() { return BitStreamWriter; }

	/**
	 * 创建子上下文——共享 ErrorContext/InternalContext 等全局字段，但把 Writer 替换为指定的
	 * 子 Writer（Reader 被清空）。典型用法是 substream / speculative write（写入一个子流，
	 * 结束后再把结果拼回父流）。
	 */
	FNetSerializationContext MakeSubContext(FNetBitStreamWriter*) const;
	/** 创建子上下文——用于切换到 substream Reader。 */
	FNetSerializationContext MakeSubContext(FNetBitStreamReader*) const;

	/** 绑定 NetTrace 收集器；Serializer 内的 UE_NET_TRACE_SCOPE 宏会写入此收集器。 */
	void SetTraceCollector(FNetTraceCollector* InTraceCollector) { TraceCollector = InTraceCollector; }
	/** 获取当前 TraceCollector；若为空则所有 trace scope 变成无操作。 */
	FNetTraceCollector* GetTraceCollector() { return TraceCollector; }

	/** 当前 context 的 ErrorContext 是否已记录错误（不含位流溢出）。 */
	bool HasError() const { return ErrorContext.HasError(); }
	/** ErrorContext 错误 OR 位流溢出——Serializer 循环用此作为"快速失败"判断。 */
	bool HasErrorOrOverflow() const;
	
	/** If an error has already been set calling this function again will be a no-op, if bDoOverFlow is true, the function will also mark the current bitstream as overflown */
	/**
	 * 报告序列化错误。
	 * @param Error       预定义的 FName（见 NetErrorContext.h 中的 GNetError_*），建议用预定义项。
	 * @param bDoOverFlow 若为 true（默认），同时把 Reader/Writer 标记为溢出，阻断后续读写；
	 *                    某些场景（例如可以恢复的错误）可传 false，只记录错误而不中断流。
	 * 副作用：若 ErrorContext 已记录过错误，仅保留首次错误（不会覆盖）。
	 */
	void SetError(const FName Error, bool bDoOverFlow = true);
	/** 取回当前已记录的错误名。若未出错则返回 NAME_None。 */
	FName GetError() const { return ErrorContext.GetError(); }

	/** Store extra information regarding the object that triggered an error. */
	/** 记录触发错误的对象 NetRefHandle，便于上层日志/Journal 指出"哪个对象失败"。 */
	void SetErrorHandleContext(const FNetRefHandle& HandleContext);
	/** 获取绑定到错误上下文的对象句柄；若未设置为 invalid handle。 */
	const FNetRefHandle& GetErrorHandleContext() const { return ErrorContext.GetObjectHandle(); }

	/** There are cases where an error is handled and reported where we want to stay calm, reset the error context and carry on */
	/** 清空 ErrorContext——调用方已吞掉错误并决定继续。注意：这**不会**清位流溢出状态。 */
	void ResetErrorContext() { ErrorContext = FNetErrorContext(); }

	/** Add entry into read journal, Name must be a static string as the pointer will be stored */
	/** 向读 Journal 追加一条记录——字符串形式。Name 必须是永久存在的静态字符串（仅存指针）。 */
	void AddReadJournalEntry(const TCHAR* Name);

	/** Add entry in to error context, Name must be a static string as the pointer will be stored */
	/** 向读 Journal 追加一条记录——基于 NetDebugName。通常在 Trace / 调试宏内调用。 */
	void AddReadJournalEntry(const FNetDebugName* DebugName);

	/** Print the ReadJournal */
	/** 导出最近 32 条 Read Journal（供 crash 日志/调试 dump）。 */
	FString PrintReadJournal();

	/** 清空 Read Journal（新帧 / 新包的开头调用）。 */
	void ResetReadJournal() { ReadJournal.Reset(); }

	/** 设置本轮写入/读取是否为对象的"首次状态"——将影响一些 Serializer（例如 InitOnly 成员）。 */
	void SetIsInitState(bool bInIsInitState) { bIsInitState = bInIsInitState; }
	/** 当前是否处在 InitState（首次复制）？ */
	bool IsInitState() const { return bIsInitState; }

	// If set, this is the changemask for the entire protocol
	/** 绑定当前协议整体的 ChangeMask 视图（各成员通过 `FNetSerializerChangeMaskParam.BitOffset` 索引）。 */
	void SetChangeMask(const FNetBitArrayView* InChangeMask) { ChangeMask = InChangeMask; }
	/** 获取 ChangeMask；若未绑定返回 nullptr——表示"全量写/读"或"不支持 ChangeMask"。 */
	const FNetBitArrayView* GetChangeMask() const { return ChangeMask; }

	/** 获取 NetBlob 接收器——重型 payload（PartialNetBlob / RPC）在反序列化路径注入此对象。 */
	INetBlobReceiver* GetNetBlobReceiver() { return NetBlobReceiver; }
	/** 设置 NetBlob 接收器（通常由 ReplicationReader 写入）。 */
	void SetNetBlobReceiver(INetBlobReceiver* InNetBlobReceiver) { NetBlobReceiver = InNetBlobReceiver; }

	/** 绑定本次读/写所属的本地连接 id（供 ObjectReference / RPC 解析 per-connection 数据使用）。 */
	void SetLocalConnectionId(uint32 InLocalConnectionId) { LocalConnectionId = InLocalConnectionId; }
	/** 获取本地连接 id；InvalidConnectionId(0) 表示未绑定。 */
	uint32 GetLocalConnectionId() const { return LocalConnectionId; }

	/**
	 * Retrieves the user data object associated with the local connection.
	 *
	 * @param ConnectionId Local connection ID.
	 * @return The user data object associated with the connection.
	 */
	/**
	 * 获取连接关联的用户数据对象（`UReplicationSystem::GetConnectionUserData`）。
	 * @param ConnectionId 本地连接 id。
	 * @return            连接关联的 UObject*，可能为 nullptr（例如 InternalContext 未设或 ConnectionId 无效）。
	 */
	IRISCORE_API UObject* GetLocalConnectionUserData(uint32 ConnectionId);

	/** 获取 NetTokenStore（const 版）——从 InternalContext->ReplicationSystem 间接拿到。 */
	IRISCORE_API const UE::Net::FNetTokenStore* GetNetTokenStore() const;
	/** 获取可变 NetTokenStore，供 serializer 在 Quantize/Serialize 时注册/写入 token。 */
	IRISCORE_API UE::Net::FNetTokenStore* GetNetTokenStore();
	/** 获取对端 NetTokenStoreState（读侧解析所需）。 */
	IRISCORE_API const UE::Net::FNetTokenStoreState* GetRemoteNetTokenStoreState() const;

	/** 绑定 InternalContext（包含 ReplicationSystem/PackageMap/内存分配器等）。 */
	void SetInternalContext(Private::FInternalNetSerializationContext* InInternalContext) { InternalContext = InInternalContext; }
	/** 访问 InternalContext。Serializer 通过它分配 dynamic state、访问 UE::Net 框架全局。 */
	Private::FInternalNetSerializationContext* GetInternalContext() { return InternalContext; }

	/** 绑定导出上下文（`FNetRefHandle`/`FNetToken`/`FNetObjectReference` 的"已导出/待导出"集合）。 */
	void SetExportContext(Private::FNetExportContext* InExportContext) { ExportContext = InExportContext; }
	/** 取回 ExportContext；为 nullptr 时 Serializer 退化为"内联导出"行为。 */
	Private::FNetExportContext* GetExportContext() { return ExportContext; }

	/** 绑定统计上下文（字节/位数/对象数的 per-connection 度量）。 */
	void SetNetStatsContext(Private::FNetStatsContext* InNetStatsContext) { NetStatsContext = InNetStatsContext; }
	/** 取回统计上下文，可能为 nullptr。 */
	Private::FNetStatsContext* GetNetStatsContext() { return NetStatsContext; }

	/** 进入/离开"默认状态初始化"模式——用于 ReplicationStateDescriptor 的默认缓冲构建路径。 */
	void SetIsInitializingDefaultState(bool bInIsInitializingDefaultState) { bIsInitializingDefaultState = bInIsInitializingDefaultState; }
	/** 当前是否处在默认状态初始化流程？ */
	bool IsInitializingDefaultState() const { return bIsInitializingDefaultState; }

	/** 设置当前处理的 PacketId（由 NetDriver 分配，用于 ACK/NACK 关联）。 */
	void SetPacketId(int32 InPacketId) { PacketId = InPacketId; }
	/** 获取当前 PacketId；-1 表示未绑定。 */
	int32 GetPacketId() const { return PacketId; }

private:
	/** 位流是否溢出（Reader 或 Writer 任一）。 */
	IRISCORE_API bool IsBitStreamOverflown() const;
	/** 把当前位流（Reader/Writer 任一已绑定者）标记为溢出，阻断后续读写。 */
	IRISCORE_API void SetBitStreamOverflow();

	/** 聚合错误报告器——存储首次错误的 FName 与触发对象 handle。 */
	FNetErrorContext ErrorContext;
	/** 环形 Read Journal（最近 32 条），服务端 crash/NACK 时 dump 客户端最近读取轨迹。 */
	FNetJournal ReadJournal;

	FNetBitStreamReader* BitStreamReader = nullptr;      // 当前读流，写入时为 nullptr
	FNetBitStreamWriter* BitStreamWriter = nullptr;      // 当前写流，读取时为 nullptr
	FNetTraceCollector* TraceCollector = nullptr;        // NetTrace 收集器（UI trace 层），可空

	Private::FInternalNetSerializationContext* InternalContext = nullptr; // 内部：RepSystem + PackageMap + 分配器
	Private::FNetExportContext* ExportContext = nullptr;                  // 批次导出跟踪（FNetRefHandle / FNetToken / FNetObjectReference）
	Private::FNetStatsContext* NetStatsContext = nullptr;                 // 带宽/对象数统计

	const FNetBitArrayView* ChangeMask = nullptr;        // 当前协议 ChangeMask（成员按 BitOffset 索引）
	INetBlobReceiver* NetBlobReceiver = nullptr ;        // 读流程接收重型 payload 的出口

	uint32 LocalConnectionId = 0;                        // 当前本地连接 id，0 = invalid
	int32 PacketId = -1;                                 // 当前 packet 序号，-1 = 未绑定

	/** Set when replicated objects send their very first state. */
	/** 对象首次复制（initial state）时置 1——影响 InitOnly/RepTag 等 Serializer 分支。 */
	uint32 bIsInitState : 1;
	/** Set only when dealing with a default state. */
	/** 处在默认状态构建/对齐流程时置 1——用于缓冲初始化，不写网络。 */
	uint32 bIsInitializingDefaultState : 1;
};

// Implementation
inline FNetSerializationContext::FNetSerializationContext(FNetBitStreamReader* InBitStreamReader, FNetBitStreamWriter* InBitStreamWriter)
: BitStreamReader(InBitStreamReader)
, BitStreamWriter(InBitStreamWriter)
, bIsInitState(0)
, bIsInitializingDefaultState(0)
{
}

inline FNetSerializationContext::FNetSerializationContext()
: FNetSerializationContext(static_cast<FNetBitStreamReader*>(nullptr), static_cast<FNetBitStreamWriter*>(nullptr))
{
}

inline FNetSerializationContext::FNetSerializationContext(FNetBitStreamReader* InBitStreamReader)
: FNetSerializationContext(InBitStreamReader, static_cast<FNetBitStreamWriter*>(nullptr))
{
}

inline FNetSerializationContext::FNetSerializationContext(FNetBitStreamWriter* InBitStreamWriter)
: FNetSerializationContext(static_cast<FNetBitStreamReader*>(nullptr), InBitStreamWriter)
{
}

inline FNetSerializationContext FNetSerializationContext::MakeSubContext(FNetBitStreamWriter* InBitStreamWriter) const
{
	// 拷贝自身（沿用 ErrorContext/Internal/Export/Stats/ChangeMask 等全部上下文），仅替换 Writer、清空 Reader
	FNetSerializationContext SubContext(*this);

	SubContext.BitStreamReader = nullptr;
	SubContext.BitStreamWriter = InBitStreamWriter;

	return SubContext;
}

inline FNetSerializationContext FNetSerializationContext::MakeSubContext(FNetBitStreamReader* InBitStreamReader) const
{
	// 拷贝并切换到子 Reader；调用方典型用法为 substream / peek 读取。
	FNetSerializationContext SubContext(*this);

	SubContext.BitStreamReader = InBitStreamReader;
	SubContext.BitStreamWriter = nullptr;

	return SubContext;
}

inline void FNetSerializationContext::SetError(const FName Error, bool bDoOverFlow)
{
	if (bDoOverFlow)
	{
		// 默认行为：把位流标记为 overflow，阻断后续读/写，上层循环会因 HasErrorOrOverflow() 提前退出
		SetBitStreamOverflow();
	}
	ErrorContext.SetError(Error);
}

inline void FNetSerializationContext::SetErrorHandleContext(const FNetRefHandle& HandleContext)
{
	ErrorContext.SetObjectHandle(HandleContext);
}

inline bool FNetSerializationContext::HasErrorOrOverflow() const
{
	return ErrorContext.HasError() || IsBitStreamOverflown();
}

}
