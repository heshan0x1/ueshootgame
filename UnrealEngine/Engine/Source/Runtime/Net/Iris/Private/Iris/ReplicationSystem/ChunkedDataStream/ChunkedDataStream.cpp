// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataStream.cpp（主 stream 门面实现）
// 模块: Iris / ReplicationSystem / ChunkedDataStream
//
// 概述：
//   UChunkedDataStream 自身不存任何业务状态，只作为 UDataStream 接口在
//   FChunkedDataWriter / FChunkedDataReader 之间的薄薄"门面/转发层"：
//     - 业务侧 API（EnqueuePayload / Dispatch* / GetPackageMap / Get/Set
//       Max*Bytes / HasError / GetQueuedByteCount / GetNumReceivedPayloadsPendingDispatch）
//       基本都是一行直转 Writer/Reader；
//     - UDataStream 接口（Init / Deinit / BeginWrite / WriteData / ReadData
//       / ProcessPacketDeliveryStatus / HasAcknowledgedAllReliableData）
//       由 UDataStreamManager 在每帧 PreSendUpdate / Read / DeliveryStatus
//       通知时调用，本类把它们一一转发给 Writer/Reader。
//
//   PIMPL 拆分理由：
//     - Writer/Reader 在 Private 头里定义，Public 头不暴露其细节，避免依赖蔓延；
//     - Writer 携带的 RingBuffer / 位图等内部状态体积大，藏在堆上；
//     - 业务侧只需要一个稳定 UCLASS 接口即可，便于 ini/Manager 配置。
// =============================================================================

#include "Iris/ReplicationSystem/ChunkedDataStream/ChunkedDataStream.h"
#include "ChunkedDataReader.h"
#include "ChunkedDataWriter.h"

// UHT 生成代码内联点（GENERATED_BODY 相关）。
#include UE_INLINE_GENERATED_CPP_BY_NAME(ChunkedDataStream)

// 子模块日志类别定义点（声明在 ChunkedDataStreamCommon.h）。
DEFINE_LOG_CATEGORY(LogIrisChunkedDataStream);

// 业务入队入口：直转 Writer。
// 注意：不做空指针保护——Writer 在 Init 中被初始化，Deinit 后置 null；
// 在生命周期内业务侧不应在 Init 之前/Deinit 之后调用本接口。
bool UChunkedDataStream::EnqueuePayload(const TSharedPtr<TArray64<uint8>>& Payload)
{
	return ChunkedWriter->EnqueuePayload(Payload);
}

// UDataStream::Init override
// 由 UDataStreamManager 在 stream 创建/握手期间调用，提供 ReplicationSystemId、
// ConnectionId、NetExports 等关键参数。
void UChunkedDataStream::Init(const UDataStream::FInitParameters& Params)
{
	Super::Init(Params);

	using namespace UE::Net::Private;

	// 每条 stream 私有的 PackageMap，业务侧通过 ExportWrite/ReadScope 绑定它使用。
	PackageMap = TObjectPtr<UIrisObjectReferencePackageMap>(NewObject<UIrisObjectReferencePackageMap>());

	// PIMPL：内部 Writer/Reader 用 new 分配（生命周期与本 UCLASS 对齐）。
	ChunkedWriter = new FChunkedDataWriter(Params);
	ChunkedReader = new FChunkedDataReader(Params);
}

// UDataStream::Deinit override：释放 Writer/Reader/PackageMap。
// 注意：Manager 会保证 Deinit 之前所有发送/接收数据已停止；不需要在这里做停发逻辑。
void UChunkedDataStream::Deinit()
{
	Super::Deinit();

	if (ChunkedWriter)
	{
		delete ChunkedWriter;
		ChunkedWriter = nullptr;
	}
	if (ChunkedReader)
	{
		delete ChunkedReader;
		ChunkedReader = nullptr;
	}

	// Release Packagemap
	// 解除 GC 引用，让 PackageMap 可被回收。
	PackageMap = nullptr;
}

// UDataStream::BeginWrite override：转发至 Writer。
// Writer 端会查 SendQueue 是否非空且未满在途窗口，决定 HasMoreData / NoData。
UDataStream::EWriteResult UChunkedDataStream::BeginWrite(const FBeginWriteParameters& Params)
{
	if (ChunkedWriter != nullptr)
	{
		return ChunkedWriter->BeginWrite(Params);
	}

	return EWriteResult::NoData;
}

// UDataStream::WriteData override：转发至 Writer。
// OutRecord 的"分片数"被 Writer 巧妙塞入指针 bits（详见 ChunkedDataWriter.cpp）。
UDataStream::EWriteResult UChunkedDataStream::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	if (ChunkedWriter != nullptr)
	{
		return ChunkedWriter->WriteData(Context, OutRecord);
	}

	return EWriteResult::NoData;
}

// UDataStream::ReadData override：转发至 Reader。
void UChunkedDataStream::ReadData(UE::Net::FNetSerializationContext& Context)
{
	if (ChunkedReader != nullptr)
	{
		return ChunkedReader->ReadData(Context);
	}
}

// UDataStream::ProcessPacketDeliveryStatus override：把 packet 投递结果交给 Writer 处理
// （Lost ⇒ 重发；Delivered ⇒ 推进窗口；Discard ⇒ 当前实现复用 Delivered 路径无独立分支，
//   实际由 Manager 决定是否调用本接口）。
void UChunkedDataStream::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* InRecord)
{
	if (ChunkedWriter)
	{
		ChunkedWriter->ProcessPacketDeliveryStatus(Status, InRecord);
	}
}

// UDataStream::HasAcknowledgedAllReliableData override：查询 Writer SendQueue 是否已空。
// Manager 据此决定是否可以安全关闭 stream（CloseStream 状态机）。
bool UChunkedDataStream::HasAcknowledgedAllReliableData() const
{
	if (ChunkedWriter)
	{
		return ChunkedWriter->HasAcknowledgedAllReliableData();
	}

	return true;
}

// 业务批量 dispatch：转发至 Reader。
UChunkedDataStream::EDispatchResult UChunkedDataStream::DispatchReceivedPayloads(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction)
{
	if (!ChunkedReader)
	{
		return EDispatchResult::NothingToDispatch;
	}

	return ChunkedReader->DispatchReceivedPayloads(DispatchPayloadFunction);
}

// 业务单条 dispatch：转发至 Reader。
UChunkedDataStream::EDispatchResult UChunkedDataStream::DispatchReceivedPayload(TFunctionRef<void(TConstArrayView64<uint8>)> DispatchPayloadFunction)
{
	if (!ChunkedReader)
	{
		return EDispatchResult::NothingToDispatch;
	}

	return ChunkedReader->DispatchReceivedPayload(DispatchPayloadFunction);
}

// 已就绪 payload 个数；Reader 在反初始化后返回 0。
uint32 UChunkedDataStream::GetNumReceivedPayloadsPendingDispatch() const
{
	if (ChunkedReader)
	{
		return ChunkedReader->GetNumReceivedPayloadsPendingDispatch();
	}

	return 0U;
}

UIrisObjectReferencePackageMap* UChunkedDataStream::GetPackageMap()
{
	return PackageMap.Get();
}

const UIrisObjectReferencePackageMap* UChunkedDataStream::GetPackageMap() const
{
	return PackageMap.Get();
}

// FChunkedDataStreamExportWriteScope：构造时把 PackageMap 绑定到 Writer 的
// PackageMapExports 容器（写模式）。析构时自动解绑。
//
// 容错：DataStream 为空 / Writer 未初始化时把所有指针置 nullptr，
// 让底层 ScopeGuard 友好地短路（业务侧若误用空指针，行为是"无操作"，不会崩溃）。
FChunkedDataStreamExportWriteScope::FChunkedDataStreamExportWriteScope(UChunkedDataStream* DataStream)
: WriteScope(DataStream ? DataStream->GetPackageMap() : nullptr, DataStream && DataStream->ChunkedWriter ? &DataStream->ChunkedWriter->PackageMapExports : nullptr)
{
}

UIrisObjectReferencePackageMap* FChunkedDataStreamExportWriteScope::GetPackageMap()
{
	return WriteScope.GetPackageMap();
}

// FChunkedDataStreamExportReadScope：构造时把 PackageMap 绑定到 Reader 的
// PackageMapExports + NetTokenResolveContext（读模式），用于 dispatch 期间反序列化对象引用。
FChunkedDataStreamExportReadScope::FChunkedDataStreamExportReadScope(UChunkedDataStream* DataStream)
: ReadScope(DataStream ? DataStream->GetPackageMap() : nullptr, DataStream && DataStream->ChunkedReader ? &DataStream->ChunkedReader->PackageMapExports : nullptr, DataStream && DataStream->ChunkedReader ? &DataStream->ChunkedReader->NetTokenResolveContext : nullptr)
{
}

UIrisObjectReferencePackageMap* FChunkedDataStreamExportReadScope::GetPackageMap()
{
	return ReadScope.GetPackageMap();
}

// 发送侧总在途字节（粗粒度）。供业务侧做发送背压。
uint32 UChunkedDataStream::GetQueuedByteCount() const
{
	if (ChunkedWriter)
	{
		return ChunkedWriter->GetQueuedBytes();
	}

	return 0U;
}

// 调整接收侧上限。Reader 内部下次 AssemblePayloadsPendingAssembly 检查时生效。
void UChunkedDataStream::SetMaxUndispatchedPayloadBytes(uint32  MaxUndispatchedPayloadBytes)
{
	if (ChunkedReader)
	{
		ChunkedReader->MaxUndispatchedPayloadBytes = MaxUndispatchedPayloadBytes;
	}
}

// 调整发送侧上限。Writer 内部下次 EnqueuePayload 比对时生效。
void UChunkedDataStream::SetMaxEnqueuedPayloadBytes(uint32 MaxEnqueuedPayloadBytes)
{
	if (ChunkedWriter)
	{
		ChunkedWriter->SendBufferMaxSize = MaxEnqueuedPayloadBytes;
	}
}

// 仅查询接收端错误态——目前只有 Reader 会进入错误态（缓冲超限/payload 损坏/exports 解析失败）。
// Writer 端的 SendBufferFull 只是返回 false，不算错误态。
bool UChunkedDataStream::HasError() const
{
	if (ChunkedReader)
	{
		return ChunkedReader->HasError();
	}

	return false;
}
