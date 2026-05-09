// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataStream.h（Public 入口）
// 模块: Iris / ReplicationSystem / ChunkedDataStream
//
// 概述：
//   UChunkedDataStream 是 UDataStream 的具体派生类（一条独立的 DataStream），
//   作用是把"无法塞进单个网络包"的大块业务 payload 切分为固定大小的 chunk
//   分多个 packet 发送，对端按序号重组为完整 payload 并交还业务层。
//
//   典型应用场景（来自架构文档）：
//     - level streaming 的关卡块/资产数据传输；
//     - save game 上传/下载；
//     - 自定义大块业务数据（任意尺寸）；
//     - 任何"单包装不下、又必须可靠交付"的 payload。
//
//   传输语义：所有 chunk 都按 11 位序号严格有序、可靠交付（丢包重传、
//   重复检测）。注：用户文档里提到的 EChunkedDataReliableStatus（Unreliable/
//   Reliable/ReliableInOrder 三档）在当前实现中并不存在——本 stream 只提供
//   单一档"可靠 + 严格有序"的传输模式（见 ChunkedDataWriter.cpp）。
//
//   ini 配置（来自 DataStream.md §2.4 示例）：
//     +DataStreams=(DataStreamName="ChunkedData",
//                   ClassName="/Script/IrisCore.ChunkedDataStream",
//                   bAutoCreate=false,bDynamicCreate=true)
//   即：默认不自动创建，业务侧通过 UReplicationSystem::OpenDataStream
//   动态打开/关闭。
//
//   导出（exports）：本 stream 拥有自己的 UIrisObjectReferencePackageMap，
//   业务在 payload 里序列化的对象引用会通过 PackageMap 捕获并随 payload
//   作为单独的"export chunk"发送，接收端解 quantize 后即可在 dispatch 时
//   解析出 UObject*。
//
// 与文档对应：
//   - Iris_Architecture.md §3.8 / §6 拓扑；
//   - ReplicationSystem.md §6.7；
//   - DataStream.md（基类管线 BeginWrite/WriteData/ReadData/Deliver）。
// =============================================================================

#pragma once

#include "Iris/DataStream/DataStream.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "ChunkedDataStream.generated.h"

namespace UE::Net
{
	struct FIrisPackageMapExports;
	namespace Private
	{
		// 私有实现：发送/接收两侧的具体实现类，藏在 Private 目录中
		// （UCLASS 仅暴露门面，PIMPL 解耦）。
		class FChunkedDataWriter;
		class FChunkedDataReader;
	}
}

/** Scope used to setup PackageMap owned by ChunkedDataStream to write and capture exports */
// FChunkedDataStreamExportWriteScope：业务侧"写 payload"作用域守卫。
// 用法：
//   {
//       FChunkedDataStreamExportWriteScope Scope(MyChunkedDataStream);
//       // 用 Scope.GetPackageMap() 配置 FArchive，序列化包含 UObject* 的 payload
//       // PackageMap 会自动捕获引用到 ChunkedWriter->PackageMapExports
//   }
//   再调用 MyChunkedDataStream->EnqueuePayload(...)，引用就会随 payload 发送。
class FChunkedDataStreamExportWriteScope
{
public:
	IRISCORE_API FChunkedDataStreamExportWriteScope(UChunkedDataStream* DataStream);
	// 取出绑好上下文的 PackageMap，业务侧用它来 << 序列化 UObject 引用。
	IRISCORE_API UIrisObjectReferencePackageMap* GetPackageMap();

private:
	// RAII：构造时进入写模式（绑 ChunkedWriter->PackageMapExports），析构时退出。
	UE::Net::FIrisObjectReferencePackageMapWriteScope WriteScope;
};

/** Scope used to setup PackageMap owned by ChunkedDataStream for reading captured exports */
// FChunkedDataStreamExportReadScope：业务侧"读 payload"作用域守卫。
// 用法：在 DispatchReceivedPayload 的回调内构造此 scope，对 PackageMap 反序列化
// 即可解析出真实 UObject*（前提是导出已被处理且引用已解析）。
class FChunkedDataStreamExportReadScope
{
public:
	IRISCORE_API FChunkedDataStreamExportReadScope(UChunkedDataStream* DataStream);
	IRISCORE_API UIrisObjectReferencePackageMap* GetPackageMap();

private:
	// RAII：构造时进入读模式（绑 ChunkedReader->PackageMapExports + NetTokenResolveContext）。
	UE::Net::FIrisObjectReferencePackageMapReadScope ReadScope;
};

/** 
 * ChunkedDataStream
 * Experimental DataStream used to split and carry large payloads with potential exports
 *
 * UCLASS 标记 MinimalAPI：UClass 注册暴露给反射系统，但成员函数需要逐个用
 * IRISCORE_API 显式标注，避免无意中扩大 dll 边界。
 */
UCLASS(MinimalAPI)
class UChunkedDataStream : public UDataStream
{
	GENERATED_BODY()

public:
	// EDispatchResult：Dispatch 调用的三种结果
	// - Ok                                 ：成功派发了一条已就绪的 payload
	// - WaitingForMustBeMappedReferences   ：payload 已就绪，但其中含有"必须解析后
	//                                        才能 dispatch"的 UObject 引用，正在 async load，
	//                                        业务侧应稍后再调一次
	// - NothingToDispatch                  ：无可派发数据（队列空 / 队首未就绪 / Reader 在
	//                                        错误状态）
	enum class EDispatchResult : uint8
	{
		Ok,
		WaitingForMustBeMappedReferences,
		NothingToDispatch
	};

	/**
	 * Enqueue Payload for sending,
	 * Object References written to the payload by using the PackageMap associated with the DataStream will be appended to the payload.
	 * @param Payload SharedPtr to Payload to send, The DataStream will hold a shared reference until the transfer is complete.
	 * @return false if SendBuffer is full
	 */
	// 业务侧主入口：把一个完整的大 payload 入发送队列。
	// - 入队时，若之前在 Export 写作用域内捕获到 PackageMap exports，会一并打包；
	// - 用 TSharedPtr 是为了让 stream 在切分/在途期间持有 payload，业务侧不需要保活；
	// - 返回 false 表示发送缓冲已满（CurrentBytesInSendQueue 超过 SendBufferMaxSize），
	//   业务侧需要回退/重试，stream 不会丢失之前已入队的 payload。
	IRISCORE_API bool EnqueuePayload(const TSharedPtr<TArray64<uint8>>& Payload);

	/**
	 * Dispatch received Payload
	 * Object References can be read from the payload through the PackageMap associated with the DataStream.
	 * @param DispatchDataChunkFunction Reference to TFunction to process Payload.
	 */
	// 业务侧接收入口（单条）：取出队首的"已完整重组并解决了所有 must-be-mapped 引用"的
	// payload，回调里业务可用 GetPackageMap() / FChunkedDataStreamExportReadScope 反序列化。
	IRISCORE_API EDispatchResult DispatchReceivedPayload(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction);

	/**
	* Dispatch all received Payloads
	* Object References can be read from the payload through the PackageMap associated with the DataStream.
	* @param DispatchDataChunkFunction Reference to TFunction to process Payload.
	*/
	// 业务侧接收入口（批量）：循环 DispatchReceivedPayload 直到队空 / Waiting / Nothing。
	// 一次 tick 内尽量榨干已就绪的 payloads。
	IRISCORE_API EDispatchResult DispatchReceivedPayloads(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction);

	/** Get the number of received payloads that are ready for dispatch */
	// 查询接收队列中已完整重组、可立刻 dispatch 的 payload 个数（不含还在重组中的）。
	IRISCORE_API uint32 GetNumReceivedPayloadsPendingDispatch() const;

	/** Get UIrisObjectReferencePackageMap associated with the DataStream */
	// 取本 stream 持有的 PackageMap（用于业务侧手动配置 FArchive，
	// 但更推荐使用 ExportWrite/ReadScope 自动 RAII 进出读写模式）。
	IRISCORE_API UIrisObjectReferencePackageMap* GetPackageMap();
	IRISCORE_API const UIrisObjectReferencePackageMap* GetPackageMap() const;

	/** Get number of payload bytes that is yet to be acknowledged */
	// 查询发送侧仍未 ACK 的总字节数（含 exports payload）。
	// 业务侧可据此做背压、UI 进度提示等。
	IRISCORE_API uint32 GetQueuedByteCount() const;

	/** 
	 * Set the maximum number of undispatched payload bytes we can have on the receiing side. 
	 * @param MaxUndispatchedPayloadBytes Stream will be set in error state and close if we have received too much data without dispatching it
	 */
	// 接收侧上限保护：限制"已收到但业务还没 dispatch"的字节数。
	// 超过则把 Reader 置为错误态（HasError 返回 true），上层应关闭本 stream。
	// 防止恶意/异常对端持续发送但本端不 dispatch 导致内存炸裂。
	IRISCORE_API void SetMaxUndispatchedPayloadBytes(uint32 MaxUndispatchedPayloadBytes);

	/** 
	* Set the maximum number of enqueued payload bytes we can have on the sending side. 
	* @param MaxEnqueuedPayloadBytes Stream will be set in error state and close if we have received too much data without dispatching it
	*/
	// 发送侧上限保护：限制 SendQueue 总字节数。超过该值时 EnqueuePayload 返回 false。
	IRISCORE_API void SetMaxEnqueuedPayloadBytes(uint32 MaxEnqueuedPayloadBytes);

	/** 
	* Returns true if the stream is in an error state and should be closed.
	*/
	// 查询本 stream 是否进入错误态（接收端缓冲超限/payload 损坏/exports 处理失败等）。
	// 上层（业务/连接管理）发现 true 后应主动 CloseStream。
	IRISCORE_API bool HasError() const;

protected:

	// UDataStream interface
	// -------------------------------------------------------------------------
	// UDataStream 接口实现（被 UDataStreamManager 调度）
	// -------------------------------------------------------------------------

	// 初始化：构造内部 PackageMap、Writer、Reader（绑定 ReplicationSystemId/ConnectionId）。
	IRISCORE_API virtual void Init(const UDataStream::FInitParameters& Params) override;
	// 反初始化：释放 Writer/Reader/PackageMap。
	IRISCORE_API virtual void Deinit() override;

	// BeginWrite：在每个 packet 开始时由 Manager 调用，返回是否有数据可写
	//   （HasMoreData / NoData）。本类仅查 SendQueue 是否非空且未达"未 ACK 上限"。
	IRISCORE_API virtual EWriteResult BeginWrite(const FBeginWriteParameters& Params) override;
	// WriteData：实际把分片串行化到 bitstream，并产出 OutRecord 给 Manager 用以 ACK 回调。
	//   record 实际上是"本 packet 写入了多少分片"的整数，复用指针 bits 存储节省内存。
	IRISCORE_API virtual EWriteResult WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord) override;

	// ReadData：解析对端 packet 中本 stream 的所有分片，按序号归档到 DataChunksPendingAssembly，
	//   能立刻按序重组的部分追加到 ReceiveQueue。
	IRISCORE_API virtual void ReadData(UE::Net::FNetSerializationContext& Context) override;
	// ProcessPacketDeliveryStatus：Manager 把 packet 的最终投递状态（Delivered / Lost）回灌
	//   到本 stream，配合之前 WriteData 产生的 record，用于"确认/重发"。
	IRISCORE_API virtual void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record) override;
	// HasAcknowledgedAllReliableData：Manager 判断本 stream 是否可以安全关闭
	//   （所有发出的可靠 chunk 都已 ACK）。
	IRISCORE_API virtual bool HasAcknowledgedAllReliableData() const override;

private:
	// Export Scope 友元：需要直接读 ChunkedWriter->PackageMapExports / ChunkedReader->...
	// 的内部成员来构造 ReadScope/WriteScope。
	friend FChunkedDataStreamExportWriteScope;
	friend FChunkedDataStreamExportReadScope;

	// 内部实现：发送侧 Writer（切分 + 在途跟踪 + ACK/Lost 处理）。
	UE::Net::Private::FChunkedDataWriter* ChunkedWriter = nullptr;
	// 内部实现：接收侧 Reader（按序号槽位重组 + 重复检测 + dispatch）。
	UE::Net::Private::FChunkedDataReader* ChunkedReader = nullptr;

	// 本 stream 私有的 PackageMap：用于在 payload 里捕获/重建 UObject 引用。
	// 标记 Transient：不参与序列化保存。
	UPROPERTY(Transient)
	TObjectPtr<UIrisObjectReferencePackageMap> PackageMap = nullptr;
};
