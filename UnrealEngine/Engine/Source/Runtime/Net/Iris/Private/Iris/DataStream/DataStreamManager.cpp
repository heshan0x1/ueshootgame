// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStreamManager.cpp —— 多路复用 + 状态机 + 位掩码序列化的核心实现
// ---------------------------------------------------------------------------------------------
// 全文件 1200+ 行，是 DataStream 模块最复杂的部分。主要内容：
//
//   1) FImpl Pimpl 内部数据结构
//        - Streams[]            ：连接持有的子流（最多 32 条，5-bit 索引）
//        - StreamSendStatus[]   ：每流的允许发送状态（Pause/Send）
//        - StreamState[]        ：每流的 6 状态生命周期（4-bit 序列化）
//        - RecordStorage[]      ：FRecord 内存池（容量 = PacketWindowSize）
//        - Records              ：TResizableCircularQueue<FRecord*>，按 packet 顺序排队
//        - DirtyStreamsMask     ：本帧待写出"状态变更"的流位掩码
//        - NetExports           ：连接级共享的 NetToken / 对象引用导出表
//
//   2) 状态机（详见 SetStreamState / HandleReceivedStreamState 的完整转移图注释）：
//        Invalid → PendingCreate → WaitOnCreateConfirmation → Open
//                                                          ↓ (RequestClose / 收到对端 PendingClose)
//                                                          PendingClose
//                                                          ↓ (HasAcknowledgedAllReliableData)
//                                                          WaitOnCloseConfirmation
//                                                          ↓ (收到对端 WaitOnCloseConfirmation 确认)
//                                                          Invalid（DestroyStream）
//
//   3) Packet 头部位布局（WriteData / ReadData）：
//        ┌──────────────────────────┬─────────────────────┬────────────────────┬──────────────────────────────────────┐
//        │ StreamCount-1 (5 bits)   │ DataStreamMask (N)  │ HasStateChange (1) │ [可选] StateChangeMask (N)           │
//        ├──────────────────────────┴─────────────────────┴────────────────────┴──────────────────────────────────────┤
//        │ [对每条 StateChangeMask 置 1 的流] State (4 bits)                                                            │
//        ├──────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
//        │ [对每条 DataStreamMask 置 1 的流] SubStream payload（由各子流 WriteData 自行决定）                          │
//        └──────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
//      其中 N = StreamCount（实际占用的子流数量；首次写入时占位写入 0，结尾再回填）。
//
//   4) WriteData / ReadData / ProcessPacketDeliveryStatus 三大主循环。
//
//   5) 握手协议：dynamic 流的 Create/Close 双向确认（PendingCreate / Open / PendingClose / WaitOnCloseConfirmation）。
// =============================================================================================

#include "Iris/DataStream/DataStreamManager.h"
#include "Iris/DataStream/DataStreamDefinitions.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/Misc/ResizableCircularQueue.h"
#include "Iris/PacketControl/PacketNotification.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/ReplicationSystem/NetExports.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataStreamManager)

// 日志辅助宏：自动带上 ReplicationSystemId 与 ConnectionId 前缀，定位多 RS / 多连接环境下的日志。
#define UE_LOG_DATASTREAM_CONN(Verbosity, Format, ...)  UE_LOG(LogIris, Verbosity, TEXT("DataStreamManager: R:%u :C%u ") Format, InitParameters.ReplicationSystemId, InitParameters.ConnectionId, ##__VA_ARGS__)

/**
 * UDataStreamManager::FImpl —— Pimpl 内部实现，所有真正的逻辑都在这里。
 *
 * 之所以拆 FImpl 而不是直接放在 UDataStreamManager 里，是为了：
 *  1. 头文件保持极简（仅 TPimplPtr<FImpl>，下游不依赖任何具体内部类型）。
 *  2. 把 NetExports / TResizableCircularQueue / NetSerialization 等内部依赖完全隔离。
 *  3. UObject 反射成本（GENERATED_BODY 元数据膨胀）只承担在 UDataStreamManager 自身，FImpl 是普通 C++ 类。
 */
class UDataStreamManager::FImpl
{
public:
	using EDataStreamState = UDataStream::EDataStreamState;

	/**
	 * CreateStream 的内部 flag。
	 *
	 *  - None                       ：常规创建（实例化 UDataStream + 初始 Open 或 PendingCreate）。
	 *  - RegisterIfStreamIsDynamic  ：对 bDynamicCreate=true 的流 **只占位** StreamIndex，不实例化 UObject。
	 *                                 在连接 Init 阶段使用（让两端都预留同一个 5-bit 索引位），
	 *                                 之后真正调用 OpenDataStream 时再实例化（Flags=None 重新走 CreateStreamFromDefinition）。
	 */
	enum class ECreateDataStreamFlags : uint8
	{
		// Create stream
		None,
		// Streams marked with bDynamicCreate will only be registered if this flag is set.
		RegisterIfStreamIsDynamic,
	};
	FRIEND_ENUM_CLASS_FLAGS(ECreateDataStreamFlags);

public:
	FImpl();
	~FImpl();

	void Init(const UDataStream::FInitParameters& InitParams);
	void Deinit();

	void Update(const FUpdateParameters& Params);

	EWriteResult BeginWrite(const UDataStream::FBeginWriteParameters& Params);
	UDataStream::EWriteResult WriteData(UE::Net::FNetSerializationContext& context, FDataStreamRecord const*& OutRecord);
	void EndWrite();
	void ReadData(UE::Net::FNetSerializationContext& context);
	void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record);
	bool HasAcknowledgedAllReliableData() const;
	
	ECreateDataStreamResult CreateStream(const FName StreamName, ECreateDataStreamFlags Flags = ECreateDataStreamFlags::None);
	void CloseStream(const FName StreamName);

	const UDataStream* GetStream(const FName StreamName) const;
	UDataStream* GetStream(const FName StreamName);

	void SetSendStatus(const FName StreamName, EDataStreamSendStatus Status);
	EDataStreamSendStatus GetSendStatus(const FName StreamName) const;

	EDataStreamState GetStreamState(const FName StreamName) const;

	UE::Net::Private::FNetExports& GetNetExports();

	void AddReferencedObjects(FReferenceCollector& Collector);

private:
	/**
	 * 双重重载 functor：用同一份 FName 既能在 TArray<UDataStream*> 中按 GetFName 查找，
	 * 又能在 TArray<FDataStreamDefinition> 中按 DataStreamName 查找。封装"按名查"以减少 lambda 重复。
	 */
	struct FindStreamByName
	{
		inline FindStreamByName(FName InName) : Name(InName) {}
		
		// 重载 1：用于 Streams 数组（TObjectPtr<UDataStream>）。注意这里比对的是 UObject 的 FName（GetFName），
		// 而非 FDataStreamDefinition::DataStreamName —— 但 NewObject 时使用了 MakeUniqueObjectName，
		// 实际名字带后缀（如 "Replication_0"），所以 IgnoreCase + bCompareNumber=false（第三参数 false）：忽略数字后缀。
		inline bool operator()(const UDataStream* Stream) const { return Stream && Name.IsEqual(Stream->GetFName(), ENameCase::IgnoreCase, false); }
		// 重载 2：用于 Definition 数组。直接 FName 比较。
		inline bool operator()(const FDataStreamDefinition& Definition) const { return Name == Definition.DataStreamName; }

	private:
		FName Name;
	};

	/**
	 * Manager 级 Record —— 一条对应一个 packet。
	 *
	 * Manager 把每条子流 WriteData 返回的 SubRecord 收集到 DataStreamRecords[StreamIndex]，
	 * 同时记下 DataStreamMask（本包哪些流写了 payload）+ DataStreamStateMask（本包哪些流写了状态变更），
	 * 之后 ACK/Lost/Discard 来临时按 mask 把 Status 分发到对应子流。
	 *
	 * TInlineAllocator<8>：8 个内联指针槽，覆盖典型场景（流数量极少），避免堆分配。
	 */
	struct FRecord : public FDataStreamRecord
	{
		// 子流 SubRecord 数组（按 StreamIndex 索引；空槽为 nullptr）
		TArray<const FDataStreamRecord*, TInlineAllocator<8>> DataStreamRecords;
		// 本 packet 哪些流写了 payload（用于 ProcessPacketDeliveryStatus 分发）
		uint32 DataStreamMask;
		// What streams carried state changes in last record
		// 本 packet 哪些流写了状态变更（用于 Lost 时的状态回滚）
		uint32 DataStreamStateMask;
	};

	void InitRecordStorage();
	void InitStream(UDataStream* Stream, FName StreamName);
	void InitStreams();
	void DestroyStream(uint32 StreamIndex);
	void SetStreamState(uint32 StreamIndex, EDataStreamState NewState);
	EDataStreamState GetStreamState(uint32 StreamIndex) const;
	ECreateDataStreamResult CreateStreamFromDefinition(const FDataStreamDefinition& Definition, ECreateDataStreamFlags Flags);
	ECreateDataStreamResult CreateStreamFromIndex(int32 StreamIndex);
	void MarkStreamStateDirty(uint32 StreamIndex);
	void HandleReceivedStreamState(UE::Net::FNetSerializationContext& Context, uint32 StreamIndex, EDataStreamState RecvdState);

private:
	// 流数量上限：5 bits 足以表达 0..31。提升此数需同步修改 FRecord 的 DataStreamMask 类型（uint32 → uint64 等）以及 ReadData/WriteData 中的 ReadBits/WriteBits 容量。
	static constexpr uint32 MaxStreamCount = 32U;
	// StreamCount 字段的位宽（5 bits 表达 0..31，序列化时再 +1 还原 1..32）。
	static constexpr uint32 StreamCountBitCount = 5U; // Enough for 32 streams
	// EDataStreamState 的位宽（4 bits 表达 0..15，足够 6 个状态）。
	static constexpr uint32 StreamStateBitCount = 4U; // Enough for 16 states

	// 连接级共享的 NetExports（NetToken / 对象引用导出表）。每条子流都通过 InitParameters.NetExports 拿到这同一个指针。
	UE::Net::Private::FNetExports NetExports;

	// We can afford reserving space for a few pointers It's unlikely we will create anything close to 16 streams.
	// 这三个数组的下标都是 5-bit StreamIndex，同步增长（SetNum 在 CreateStreamFromDefinition 中统一执行）。
	TArray<TObjectPtr<UDataStream>> Streams;       // 子流对象
	TArray<EDataStreamSendStatus> StreamSendStatus; // 每流允许发送（Pause/Send）
	TArray<EDataStreamState> StreamState;           // 每流当前生命周期状态

	// FRecord 内存池：容量 = PacketWindowSize。Records 队列里保存的 FRecord* 永远指向这块池中的元素（轮转复用）。
	TArray<FRecord> RecordStorage;
	// FIFO 包顺序队列：每次 WriteData 入队一个 FRecord*；每次 ProcessPacketDeliveryStatus 出队一个。
	// 最多同时持有 PacketWindowSize 条未 ACK 的 record。
	TResizableCircularQueue<FRecord*> Records;

	// 缓存 Init 时拿到的连接级运行期参数（含 NetExports 已被替换为本对象的 &NetExports 而非外部传入）。
	UDataStream::FInitParameters InitParameters;

	// 本帧待写出"状态变更"的位掩码 —— 每位对应一条流；MarkStreamStateDirty 设置，WriteData 写入后清零。
	uint32 DirtyStreamsMask = 0U;
};

// 编译期检查：EDataStreamState 必须能装入 4 bits（0..15）。Count=6，安全。
static_assert((uint32)(UDataStream::EDataStreamState::Count) <= 15U, "EDataStreamState must fit in 4 bits.");

// 默认构造：CDO 路径不分配 Impl（CDO 不会被 Init 也不会写包），普通实例分配。
UDataStreamManager::UDataStreamManager()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Impl = MakePimpl<FImpl>();
	}
}

UDataStreamManager::~UDataStreamManager()
{
}

// Init：把外部 InitParams 与 this 一起重打包成 FInitParameters，使子流 Init 时的 DataStreamManager 指针指向自己。
void UDataStreamManager::Init(const UDataStream::FInitParameters& InitParams)
{
	UDataStream::FInitParameters Params(this, InitParams);
	Impl->Init(Params);
}

void UDataStreamManager::Deinit()
{
	Impl->Deinit();
}

void UDataStreamManager::Update(const FUpdateParameters& Params)
{
	Impl->Update(Params);
}

UDataStream::EWriteResult UDataStreamManager::BeginWrite(const UDataStream::FBeginWriteParameters& Params)
{
	return Impl->BeginWrite(Params);
}

void UDataStreamManager::EndWrite()
{
	Impl->EndWrite();
}

UDataStream::EWriteResult UDataStreamManager::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	return Impl->WriteData(Context, OutRecord);
}

void UDataStreamManager::ReadData(UE::Net::FNetSerializationContext& Context)
{
	return Impl->ReadData(Context);
}

void UDataStreamManager::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record)
{
	return Impl->ProcessPacketDeliveryStatus(Status, Record);
}

bool UDataStreamManager::HasAcknowledgedAllReliableData() const
{
	return Impl->HasAcknowledgedAllReliableData();
}

// 静态：仅查 ini，不依赖具体连接（CDO 即可）。
bool UDataStreamManager::IsKnownStreamDefinition(const FName StreamName)
{
	const UDataStreamDefinitions* StreamDefinitions = GetDefault<UDataStreamDefinitions>();
	return StreamDefinitions->FindDefinition(StreamName) != nullptr;
}

ECreateDataStreamResult UDataStreamManager::CreateStream(const FName StreamName)
{
	return Impl->CreateStream(StreamName);
}

UDataStream* UDataStreamManager::GetStream(const FName StreamName)
{
	return Impl->GetStream(StreamName);
}

const UDataStream* UDataStreamManager::GetStream(const FName StreamName) const
{
	return Impl->GetStream(StreamName);
}

void UDataStreamManager::CloseStream(const FName StreamName)
{
	Impl->CloseStream(StreamName);
}

UDataStream::EDataStreamState UDataStreamManager::GetStreamState(const FName StreamName) const
{
	return Impl->GetStreamState(StreamName);
}

void UDataStreamManager::SetSendStatus(const FName StreamName, EDataStreamSendStatus Status)
{
	return Impl->SetSendStatus(StreamName, Status);
}

EDataStreamSendStatus UDataStreamManager::GetSendStatus(const FName StreamName) const
{
	return Impl->GetSendStatus(StreamName);
}

UE::Net::Private::FNetExports& UDataStreamManager::GetNetExports()
{
	return Impl->GetNetExports();
}

// GC 引用收集：把 Streams 数组里的所有子流标记为引用根，防止它们在还未销毁前被 GC 回收。
// FImpl 是普通 C++ 类，GC 不会自动遍历内部 TObjectPtr 数组，所以必须显式上报。
void UDataStreamManager::AddReferencedObjects(UObject* Object, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Object, Collector);

	UDataStreamManager* StreamManager = CastChecked<UDataStreamManager>(Object);
	if (FImpl* Impl = StreamManager->Impl.Get())
	{
		Impl->AddReferencedObjects(Collector);
	}
}

// =============================================================================================
// FImpl —— 真正的内部实现
// =============================================================================================

UDataStreamManager::FImpl::FImpl()
{
}

UDataStreamManager::FImpl::~FImpl()
{
}

/**
 * 连接级初始化。
 *
 * 步骤：
 *  1) 缓存 InitParams 并把 NetExports 指针替换为本对象的 &NetExports（让所有子流共享同一个 Manager 级导出表）。
 *  2) InitRecordStorage：分配 PacketWindowSize 个 FRecord，初始化 TResizableCircularQueue 为空（容量 = PacketWindowSize）。
 *  3) InitStreams：FixupDefinitions（首次） + 按 ini 创建/占位所有 bAutoCreate / bDynamicCreate 的流。
 */
void UDataStreamManager::FImpl::Init(const UDataStreamManager::FInitParameters& InitParams)
{
	InitParameters = InitParams;
	InitParameters.NetExports = &NetExports;

	InitRecordStorage();
	InitStreams();
}

/**
 * 连接级反初始化。
 *
 * 步骤：
 *  1) 把 Records 队列里所有未 ACK 的 record 用 EPacketDeliveryStatus::Discard 一一派发出去 ——
 *     让每条子流都收到 Discard 通知，可以安全释放自己的 record 内存。
 *  2) 对所有 Streams 调用 Deinit + MarkAsGarbage（让 GC 回收）。
 *  3) Reset 三个 per-stream 数组。注意 RecordStorage / Records / DirtyStreamsMask 不在这里 reset
 *     —— 因为 Manager 的生命周期与连接相同，Deinit 之后通常对象就被销毁了。
 */
void UDataStreamManager::FImpl::Deinit()
{
	// Discard all records
	// 注意循环条件：每次循环都重新求 Records.Count()，因为 ProcessPacketDeliveryStatus 内部会 Pop。
	// 但写法上 RecordEndIt 在循环开始时被固定，等价于"已知最多迭代 RecordEndIt 次"，配合内部 Pop 正好走完队列。
	for (SIZE_T RecordIt = 0, RecordEndIt = Records.Count(); RecordIt != RecordEndIt; ++RecordIt)
	{
		const FDataStreamRecord* const Record = Records.Peek();
		ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus::Discard, Record);
	}

	for (UDataStream* Stream : Streams)
	{
		if (IsValid(Stream))
		{
			Stream->Deinit();
			Stream->MarkAsGarbage();
		}
	}

	Streams.Reset();
	StreamSendStatus.Reset();
	StreamState.Reset();
}

// 把 Update 简单分发给所有有效子流。
void UDataStreamManager::FImpl::Update(const FUpdateParameters& Params)
{
	for (UDataStream* Stream : Streams)
	{
		if (IsValid(Stream))
		{
			Stream->Update(Params);
		}
	}
}

/**
 * 销毁单条流（StreamIndex 槽位）。
 *
 * 仅由状态机在 WaitOnCloseConfirmation → Invalid 转移时调用（Close 握手完成）。
 * 把 Streams 槽位置 nullptr，但保留数组长度（其它流的 StreamIndex 不变，避免重排打乱位掩码）。
 */
void UDataStreamManager::FImpl::DestroyStream(uint32 StreamIndex)
{
	UDataStream* Stream = Streams[StreamIndex];
	if (IsValid(Stream))
	{
		Stream->Deinit();
		Stream->MarkAsGarbage();
		Streams[StreamIndex] = nullptr;
		StreamState[StreamIndex] = EDataStreamState::Invalid;
		StreamSendStatus[StreamIndex] = EDataStreamSendStatus::Pause;
	}
}

/**
 * BeginWrite —— 对所有非 Pause 子流调用 BeginWrite，聚合返回值。
 *
 * 聚合规则：CombinedWriteResult = HasMoreData if 任一子流返回 HasMoreData; else 取最后一次 WriteResult。
 * 起始值：DirtyStreamsMask != 0 → HasMoreData（保证状态变更总是会被写出），否则 NoData。
 *
 * 没有任何子流时直接返回 NoData（连接初始化失败的兜底）。
 */
UDataStreamManager::EWriteResult UDataStreamManager::FImpl::BeginWrite(const UDataStream::FBeginWriteParameters& Params)
{
	const SIZE_T StreamCount = Streams.Num();
	if (StreamCount == 0)
	{
		return EWriteResult::NoData;
	}

	TObjectPtr<UDataStream>* StreamData = Streams.GetData();
	const EDataStreamSendStatus* SendStatusData = StreamSendStatus.GetData();
	const EDataStreamState* StreamStateData = StreamState.GetData();

	// 起始值：若本帧有状态变更（DirtyStreamsMask!=0）则保守认为"还有数据要写"。
	UDataStream::EWriteResult CombinedWriteResult = DirtyStreamsMask == 0U ? UDataStream::EWriteResult::NoData : UDataStream::EWriteResult::HasMoreData;

	for (SIZE_T StreamIt = 0, StreamEndIt = StreamCount; StreamIt != StreamEndIt; ++StreamIt)
	{
		UDataStream* Stream = StreamData[StreamIt];
		const EDataStreamSendStatus SendStatus = SendStatusData[StreamIt];
		if (SendStatus == EDataStreamSendStatus::Pause)
		{
			continue;
		}

		const EWriteResult WriteResult = Stream->BeginWrite(Params);
		// 聚合：HasMoreData 优先级最高（任一为 HasMoreData → 整体 HasMoreData）。
		CombinedWriteResult = (CombinedWriteResult == EWriteResult::HasMoreData || WriteResult == EWriteResult::HasMoreData) ? EWriteResult::HasMoreData : WriteResult;
	}

	return CombinedWriteResult;
}

/**
 * EndWrite —— 与 BeginWrite 严格配对，对所有非 Pause 子流调用 EndWrite。
 *
 * 注意：此处不再判断 EDataStreamState（不像 WriteData 仅对 Open/PendingClose 调用），
 * 因为 EndWrite 是 BeginWrite 的回收对，BeginWrite 已经过 Pause 过滤。
 */
void UDataStreamManager::FImpl::EndWrite()
{
	const SIZE_T StreamCount = Streams.Num();

	TObjectPtr<UDataStream>* StreamData = Streams.GetData();
	const EDataStreamSendStatus* SendStatusData = StreamSendStatus.GetData();

	for (SIZE_T StreamIt = 0, StreamEndIt = StreamCount; StreamIt != StreamEndIt; ++StreamIt)
	{
		UDataStream* Stream = StreamData[StreamIt];
		const EDataStreamSendStatus SendStatus = SendStatusData[StreamIt];
		if (SendStatus == EDataStreamSendStatus::Pause)
		{
			continue;
		}

		Stream->EndWrite();
	}
}

/**
 * WriteData —— 主写包入口（最复杂的方法）。
 *
 * 流程：
 *   ① 边界检查：流为空 / Records 队列已满（PacketWindowSize 个未 ACK） → 返回 NoData。
 *   ② NetExports：InitExportRecordForPacket + MakeExportScope（RAII，函数末尾 CommitExportsToRecord）。
 *   ③ 在 Manager substream 写入 packet header 占位：
 *        a. StreamCount-1 (5 bits)         占位 0，结尾回填
 *        b. DataStreamMask  (StreamCount)  占位 0，结尾回填
 *        c. HasStreamsWithDirtyState (1 bit)
 *        d. [可选] DirtyStreamsMask (StreamCount bits)
 *   ④ 若 bHasStreamsWithDirtyState：遍历每个 dirty 流，写 4-bit EDataStreamState；并推进发送侧状态机
 *      （PendingCreate → WaitOnCreateConfirmation；PendingClose+reliable 全 ACK → WaitOnCloseConfirmation）。
 *   ⑤ ManagerStream 溢出 → 全部丢弃返回 NoData。
 *   ⑥ 主循环：对每条 SendStatus!=Pause 且 State∈{Open,PendingClose} 的流：
 *        - 创建 SubBitStream（substream of substream）+ SubContext；
 *        - 调用 Stream->WriteData(SubContext, &SubRecord)；
 *        - 失败/NoData → 丢弃 SubBitStream（强制 SubRecord==nullptr）；
 *        - 写入位数 > 0 → 置 DataStreamMask 对应位 + 保存 SubRecord 到 TempRecord.DataStreamRecords[StreamIdx]；
 *        - CommitSubstream 把 SubBitStream 拼回 ManagerStream。
 *   ⑦ 既无业务数据又无状态变更 → DiscardSubstream 整个 ManagerStream（不消耗带宽）。
 *   ⑧ 否则 Seek 回 header 起点回填 (a)(b)，CommitSubstream 提交到外层 BitStream。
 *   ⑨ TempRecord MoveTemp 到 RecordStorage 池槽位（Records.Enqueue_GetRef），OutRecord = 该指针。
 *   ⑩ NetExports.CommitExportsToRecord + PushExportRecordForPacket（NetToken 导出绑定到 record，便于 ACK/Lost 同步）。
 */
UDataStreamManager::EWriteResult UDataStreamManager::FImpl::WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord)
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	const int32 StreamCount = Streams.Num();
	if (StreamCount <= 0)
	{
		return EWriteResult::NoData;
	}

	// Is the packet window full? Unexpected.
	// 包窗口满：调用方应先 ProcessPacketDeliveryStatus 释放 record 才能继续 Write。出现就是逻辑错误。
	if (Records.Count() == Records.AllocatedCapacity())
	{
		ensureMsgf(false, TEXT("DataStreamManager record storage is full."));
		return EWriteResult::NoData;
	}

	// Init export record
	// 为本 packet 创建一条新的 NetExports 记录（NetToken 导出会被收集到这条记录里）。
	NetExports.InitExportRecordForPacket();

	// Setup export context for this packet
	// FExportScope 是 RAII：构造时把 BatchExports 装入 Context 的 ExportContext；析构时自动 pop。
	FNetExportContext::FBatchExports CurrentPacketBatchExports;
	FNetExports::FExportScope ExportScope = NetExports.MakeExportScope(Context, CurrentPacketBatchExports);

	// 临时 record（栈上构造），最终 MoveTemp 到 RecordStorage 池中槽位。
	FRecord TempRecord;
	TempRecord.DataStreamRecords.SetNumZeroed(StreamCount);
	FDataStreamRecord const** TempStreamRecords = TempRecord.DataStreamRecords.GetData();

	// 在外层 BitStream 上开一个 ManagerStream substream（写完之后整体 commit / discard）。
	FNetBitStreamWriter ManagerStream = Context.GetBitStreamWriter()->CreateSubstream();
	// This will write the number of bits required for the StreamBitCount
	// 占位写入 StreamCount-1 (5 bits)；如果决定丢弃整个 ManagerStream（无业务数据 & 无状态变更）则不会被读到。
	ManagerStream.WriteBits(0U, StreamCountBitCount);
	// Will be rewritten later to contain a bit mask for all streams that have written data.
	// 占位写入 DataStreamMask (StreamCount bits)；末尾回填。
	ManagerStream.WriteBits(0U, StreamCount);

	// 1 bit：是否有任何流的状态发生变更。配合 DirtyStreamsMask 使用。
	const bool bHasStreamsWithDirtyState = DirtyStreamsMask != 0U;
	if (ManagerStream.WriteBool(bHasStreamsWithDirtyState))
	{
		// 写出哪些流有状态变更（StreamCount bits 位掩码）。
		ManagerStream.WriteBits(DirtyStreamsMask, StreamCount);
	}

	TObjectPtr<UDataStream>* StreamData = Streams.GetData();
	const EDataStreamSendStatus* SendStatusData = StreamSendStatus.GetData();

	// 起始聚合值：若有状态变更，至少返回 Ok（确保上层把这一包发出去）；否则 NoData（本帧无数据）。
	UDataStream::EWriteResult CombinedWriteResult = bHasStreamsWithDirtyState ? UDataStream::EWriteResult::Ok : UDataStream::EWriteResult::NoData;

	// Write rare datastream state changes
	// 写每个 dirty 流的 4-bit 状态值，并 **推进发送侧状态机**（PendingCreate → WaitOnCreateConfirmation 等）。
	if (bHasStreamsWithDirtyState)
	{
		// 备份一份旧状态（变量目前未使用，但留作将来"回滚状态机"的占位）。
		TArray<EDataStreamState> OldStreamState = StreamState;
		for (uint32 StreamIt = 0U, StreamEndIt = StreamCount, CurrentStreamMask = 1U; StreamIt != StreamEndIt; ++StreamIt, CurrentStreamMask += CurrentStreamMask)
		{
			if (DirtyStreamsMask & CurrentStreamMask)
			{
				const EDataStreamState State = GetStreamState(StreamIt);
				// Write state
				// 4-bit 状态值。注意：这是写入"当前"状态，状态机的转移在写入之后立即推进。
				ManagerStream.WriteBits((uint32)State, StreamStateBitCount);
				UE_LOG_DATASTREAM_CONN(Verbose, TEXT("WriteStreamState for StreamIndex: %u, State: %s"), StreamIt, LexToString(State));
				switch (State)
				{
					case EDataStreamState::PendingCreate:
					{
						// If we would like to add more data for create, this would be the spot.
						// 当前 PendingCreate（创建请求待发） → 写出后转 WaitOnCreateConfirmation（等待对端确认）。
						SetStreamState(StreamIt, EDataStreamState::WaitOnCreateConfirmation);
						break;
					}
					case EDataStreamState::PendingClose:
					{
						// For now, if we have no data to flush we can go directly to WaitOnCloseConfirmation
						// 关闭路径：只有 reliable 全 ACK 才能转 WaitOnCloseConfirmation；否则等下次 dirty 推进。
						UDataStream* Stream = Streams[StreamIt];
						if (Stream->HasAcknowledgedAllReliableData())
						{
							SetStreamState(StreamIt, EDataStreamState::WaitOnCloseConfirmation);
						}
						break;
					}
					default:
					{
						break;
					}
				}
			}
		}
	}

	// If we can't fit our header we can't fit anything else either.
	// 头部都写不下，连 payload 都不可能写下；放弃整个 ManagerStream。
	if (ManagerStream.IsOverflown())
	{
		Context.GetBitStreamWriter()->DiscardSubstream(ManagerStream);
		return EWriteResult::NoData;
	}

	// 主循环：遍历每条流，对 SendStatus!=Pause 且 State∈{Open, PendingClose} 的流调用 WriteData。
	uint32 DataStreamMask = 0;
	for (uint32 StreamIt = 0U, StreamEndIt = StreamCount, CurrentStreamMask = 1U; StreamIt != StreamEndIt; ++StreamIt, CurrentStreamMask += CurrentStreamMask)
	{
		UDataStream* Stream = StreamData[StreamIt];
		const EDataStreamSendStatus SendStatus = SendStatusData[StreamIt];
		if (SendStatus == EDataStreamSendStatus::Pause)
		{
			continue;
		}

		// We only write stream data of the stream is considered is open
		// 只有 Open / PendingClose 的流可以写出业务数据（PendingClose 仍在 flush 阶段）。
		// PendingCreate / WaitOnCreateConfirmation / WaitOnCloseConfirmation / Invalid 的流跳过。
		const EDataStreamState State = StreamState[StreamIt];
		if (!(State == EDataStreamState::Open || State == EDataStreamState::PendingClose))
		{
			continue;
		}

		// 为该子流开一个嵌套 substream + sub-context（共享同一个 NetExports）。
		FNetBitStreamWriter SubBitStream = ManagerStream.CreateSubstream();
		FNetSerializationContext SubContext = Context.MakeSubContext(&SubBitStream);

		const FDataStreamRecord* SubRecord = nullptr;
		const EWriteResult WriteResult = Stream->WriteData(SubContext, SubRecord);
		if (WriteResult == EWriteResult::NoData || SubContext.HasError())
		{
			// 子流声称没数据 / 写入出错：必须保证 SubRecord==nullptr（契约约束）。
			checkf(SubRecord == nullptr, TEXT("DataStream '%s' provided a record despite errors or returning NoData."), ToCStr(Stream->GetFName().GetPlainNameString()));
			ManagerStream.DiscardSubstream(SubBitStream);

			if (SubContext.HasError())
			{
				// 致命错误向上冒泡到外层 Context；break 掉，让本 packet 写完已有部分。
				Context.SetError(SubContext.GetError(), false);
				break;
			}
			else
			{
				continue;
			}
		}

		// Only update DataStreamMask if data was written. 
		// 子流可能返回 Ok 但实际没写任何位；此时不置 mask、不保存 record。
		if (SubBitStream.GetPosBits() > 0U)
		{
			DataStreamMask |= CurrentStreamMask;		
			TempStreamRecords[StreamIt] = SubRecord;
		}
		else
		{
			// 写了 0 位却给了 record：契约违反，发出 ensureMsgf。
			ensureMsgf(SubRecord == nullptr, TEXT("DataStream '%s' provided a record despite not writing any data."), ToCStr(Stream->GetFName().GetPlainNameString()));
		}

		ManagerStream.CommitSubstream(SubBitStream);

		// Set CombinedWriteResult to HasMoreData if any of the result variables is HasMoreData, otherwise take the WriteResult, which will be 'Ok'.
		// 聚合规则：HasMoreData 优先级最高，否则取最后一次 WriteResult（通常是 Ok）。
		CombinedWriteResult = (CombinedWriteResult == EWriteResult::HasMoreData || WriteResult == EWriteResult::HasMoreData) ? EWriteResult::HasMoreData : WriteResult;
	}

	if (!(DataStreamMask || bHasStreamsWithDirtyState))
	{
		// 没有任何流写了 payload，也没有状态变更 —— 整个 ManagerStream 是空头，丢弃。
		Context.GetBitStreamWriter()->DiscardSubstream(ManagerStream);
		// Technically we could also return EWriteResult::HasMoreData
		CombinedWriteResult = EWriteResult::NoData;
	}
	else
	{
		// Fixup manager header
		// 回填 header：seek 到起始位置，覆盖前面占位的两个字段，然后 seek 回末端 commit。
		{
			const uint32 CurrentBitPos = ManagerStream.GetPosBits();
			ManagerStream.Seek(0);
			// 编码：StreamCount-1 表示 1..32（5-bit 装得下）。读取端再 +1 还原。
			ManagerStream.WriteBits(StreamCount - 1U, StreamCountBitCount);
			ManagerStream.WriteBits(DataStreamMask, StreamCount);
			ManagerStream.Seek(CurrentBitPos);
			Context.GetBitStreamWriter()->CommitSubstream(ManagerStream);
		}

		// Fixup and store record
		// 把 TempRecord 落到 RecordStorage 池中（Enqueue_GetRef 返回池中槽位指针），并交还给上层做 ACK 跟踪。
		TempRecord.DataStreamMask = DataStreamMask;
		TempRecord.DataStreamStateMask = DirtyStreamsMask;
		// 状态变更已经写入本包；若包丢失会在 ProcessPacketDeliveryStatus 回滚。
		DirtyStreamsMask = 0U;

		FRecord*& Record = Records.Enqueue_GetRef();
		*Record = MoveTemp(TempRecord);

		OutRecord = Record;

		// Push exports and update export record
		// 把本包通过 ExportScope 收集到的导出条目"提交"到 record，并把 record push 到队列以便 ACK/Lost 时回放。
		NetExports.CommitExportsToRecord(ExportScope);
		NetExports.PushExportRecordForPacket();
	}

	return CombinedWriteResult;
}

// 标记某个流"状态需要写到下一个 packet"。位掩码每位对应一条流的 5-bit 索引。
void UDataStreamManager::FImpl::MarkStreamStateDirty(uint32 StreamIndex)
{
	DirtyStreamsMask |= 1U << StreamIndex;
}

// 按 StreamIndex 直接读取状态数组。
UDataStream::EDataStreamState UDataStreamManager::FImpl::GetStreamState(uint32 StreamIndex) const
{
	return StreamState[StreamIndex];
}

/**
 * SetStreamState —— 显式状态机转移（本端发起的转移）。
 *
 * 完整转移图（goto AcceptStateChange 是合法转移；其它非法转移落到末尾的 ensure(false)）：
 *
 *   Invalid:
 *      → Invalid                  ：拒绝对端 PendingCreate（本端写出 Invalid 让对端知道）
 *      → PendingCreate            ：本端调用 CreateStream 启动握手
 *
 *   PendingCreate:
 *      → Invalid                  ：本端在写出前撤销（CloseStream 的 PendingCreate 分支）
 *      → WaitOnCreateConfirmation ：本端 WriteData 写出 PendingCreate 之后
 *
 *   WaitOnCreateConfirmation:
 *      → PendingCreate            ：投递丢失（Lost），乐观回滚重发
 *      → PendingClose             ：API CloseStream 中途取消
 *      → Open                     ：收到对端 Open / PendingCreate 确认
 *      → Invalid                  ：收到对端 Invalid（对端拒绝创建）
 *
 *   Open:
 *      → PendingClose             ：本端 RequestClose 或 收到对端 PendingClose
 *      → Open                     ：收到对端再次确认（幂等，nothing to do）
 *
 *   PendingClose:
 *      → PendingClose             ：再次收到对端 PendingClose（可触发 dirty 重写）
 *      → WaitOnCloseConfirmation  ：reliable 全 ACK 后准备清理
 *      → Open                     ：收到对端 Open（对端尚未感知本端的 close）—— 忽略
 *
 *   WaitOnCloseConfirmation:
 *      → PendingClose             ：投递丢失，回滚重发
 *      → WaitOnCloseConfirmation  ：对端确认（幂等）
 *      → Invalid                  ：对端确认 + 本端清理 → DestroyStream
 *
 * 任何其它组合都视为非法 —— 触发 ensure(false) 并 reject（不修改 StreamState）。
 *
 * 副作用：成功的转移会同时 MarkStreamStateDirty —— 让下一次 WriteData 把新状态写到 packet。
 */
void UDataStreamManager::FImpl::SetStreamState(uint32 StreamIndex, EDataStreamState NewState)
{
	const EDataStreamState CurrentState = StreamState[StreamIndex];

	switch (CurrentState)
	{
		case EDataStreamState::Invalid:
		{
			if (NewState == EDataStreamState::Invalid)
			{
				// This means that we have rejected a PendingCreate request
				// 拒绝对端 PendingCreate：保持 Invalid 但要写出 Invalid 让对端知道我们拒绝了。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::PendingCreate)
			{
				// Next write will respond with PendingCreate to confirm
				// 本端发起创建：dirty 后下一帧 WriteData 写出 PendingCreate。
				goto AcceptStateChange;
			}
		}
		break;
		
		case EDataStreamState::PendingCreate:
		{
			if (NewState == EDataStreamState::Invalid)
			{
				// Local request to close stream that has not yet been sent.
				// 本端在 PendingCreate 写出前撤销：直接销毁，无需通知对端。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::WaitOnCreateConfirmation)			
			{
				// We have sent PendingCreate request
				// 已写出 PendingCreate，进入等待对端确认。
				goto AcceptStateChange;
			}
		}
		break;
		
		case EDataStreamState::WaitOnCreateConfirmation:
		{
			if (NewState == EDataStreamState::PendingCreate)
			{
				// We have dropped WaitOnCreateConfirmation (or PendingCreate)
				// 投递丢失（Lost）→ 退回 PendingCreate 等下次重发。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::PendingClose)
			{
				// API call to close
				// 等确认期间被 API 取消 —— 等于直接发起关闭。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::Open)
			{
				// Received PendingCreate/Open from other side to confirm
				// 收到对端确认 → Open，握手完成。
				goto AcceptStateChange;

			}
			else if (NewState == EDataStreamState::Invalid)
			{
				// Received reject from other side.
				// 对端拒绝创建 → 直接 Invalid。
				goto AcceptStateChange;
			}
		}
		break;

		case EDataStreamState::Open:
		{
			if (NewState == EDataStreamState::PendingClose)
			{
				// API call to close or request from other side to close
				// 本端 RequestClose 或 收到对端 PendingClose。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::Open)
			{
				// Received PendingCreate/Open from other side to confirm
				// nothing should be done.
				// 已 Open 还收到 Open（对端二次确认）—— 幂等，直接 return（不 dirty）。
				return;
			}
		}
		break;

		case EDataStreamState::PendingClose:
		{
			if (NewState == EDataStreamState::PendingClose)
			{
				// API call to close or request from other side to close
				// 已经在 PendingClose 又收到 PendingClose —— 触发 dirty 重写以推进对端状态机。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::WaitOnCloseConfirmation)
			{
				// We are flushed and can cleanup
				// reliable 全部 ACK，可进入清理阶段。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::Open)
			{
				// Received PendingCreate/Open from other side to confirm
				// nothing should be done.
				// 处于 PendingClose 又收到对端 Open（对端尚未收到 close）—— 忽略。
				return;
			}
		}
		break;

		case EDataStreamState::WaitOnCloseConfirmation:
		{
			if (NewState == EDataStreamState::PendingClose)
			{
				// We dropped PendingClose
				// 关闭包丢失 → 退回 PendingClose 重发。
				goto AcceptStateChange;				
			}
			else if (NewState == EDataStreamState::WaitOnCloseConfirmation)
			{
				// Other side has confirmed close
				// 对端确认（再次进入 WaitOnCloseConfirmation 表示 ACK 路径完成）。
				goto AcceptStateChange;
			}
			else if (NewState == EDataStreamState::Invalid)
			{
				// We can cleanup
				// 双向都已确认 → 销毁流。
				goto AcceptStateChange;
			}
		}
		break;

		default:
		break;
	};

	// 落到这里说明转移非法 —— 触发 ensure 帮助调试，但不更改状态（保持稳健）。
	UE_LOG_DATASTREAM_CONN(Verbose, TEXT("SetDataStreamState Reject: for StreamIndex: %u, CurrentState: %s, NewState: %s"), StreamIndex, LexToString(CurrentState), LexToString(NewState));
	ensure(false);
	return;

AcceptStateChange:
	UE_LOG_DATASTREAM_CONN(Verbose, TEXT("SetDataStreamState Accept: for StreamIndex: %u, CurrentState: %s, NewState: %s"), StreamIndex, LexToString(CurrentState), LexToString(NewState));
	// 更新状态 + 标 dirty（让下一帧 WriteData 把新状态写到 packet）。
	StreamState[StreamIndex] = NewState;
	MarkStreamStateDirty(StreamIndex);
	return;
}

/**
 * HandleReceivedStreamState —— 处理"对端发来的状态变更"。
 *
 * 与 SetStreamState 不同：本方法接收**收到的** 状态值，需要根据当前本地状态决定如何转移。
 *
 * 典型场景（RecvdState → CurrentState 决策）：
 *
 *   RecvdState=PendingCreate:
 *     CurrentState=Invalid                    → 本端镜像 CreateStreamFromIndex（被动创建）
 *     CurrentState=WaitOnCreateConfirmation   → 对端确认创建 → 本端转 Open
 *     其它                                    → 报错（HandleUnexpectedState）
 *
 *   RecvdState=Open:
 *     CurrentState=WaitOnCreateConfirmation   → 握手完成 → Open
 *     CurrentState=Open                       → 幂等
 *
 *   RecvdState=PendingClose:
 *     CurrentState=PendingCreate              → 罕见竞态：先 WaitOnCreateConfirmation 再 PendingClose
 *     CurrentState=WaitOnCreateConfirmation/Open → PendingClose
 *     CurrentState=PendingClose               → reliable 全 ACK → WaitOnCloseConfirmation；否则 dirty
 *     CurrentState=WaitOnCloseConfirmation    → 仅 dirty（对端可能还在 flush）
 *
 *   RecvdState=WaitOnCloseConfirmation:
 *     CurrentState=WaitOnCloseConfirmation    → Invalid + DestroyStream（双方都已确认关闭）
 *     CurrentState=PendingClose               → reliable 全 ACK → WaitOnCloseConfirmation
 *
 *   RecvdState=Invalid:
 *     CurrentState=Invalid                    → 幂等
 *     CurrentState=WaitOnCreateConfirmation   → 对端拒绝创建 → 走完整关闭流程后销毁
 *     CurrentState=WaitOnCloseConfirmation    → 对端确认关闭 → 销毁
 *
 * 收到非法组合 → HandleUnexpectedState：写错误到 Context（导致连接断开）+ ensure(false)。
 */
void UDataStreamManager::FImpl::HandleReceivedStreamState(UE::Net::FNetSerializationContext& Context, uint32 StreamIndex, EDataStreamState RecvdState)
{
	UDataStream* DataStream = Streams[StreamIndex];
	const EDataStreamState CurrentState = GetStreamState(StreamIndex);

	// 公共错误处理：把错误标记到 Context，外层会停止反序列化并断开连接。
	auto HandleUnexpectedState = [&CurrentState, &RecvdState, &StreamIndex, &Context, this]()
	{
		UE_LOG_DATASTREAM_CONN(Error, TEXT("Received invalid DataStream State: %s for StreamIndex: %u, while in State: %s"), LexToString(RecvdState), StreamIndex, LexToString(CurrentState));
		Context.SetError(TEXT("Invalid DataStreamState"));
		// Just for log attention
		ensure(false);
	};

	switch (RecvdState)
	{
		case EDataStreamState::PendingCreate:
		{
			// PendingCreate is received to request or confirm open/create
			// 收到 PendingCreate：要么对端发起创建（本端 Invalid），要么对端的 Open 确认（本端 WaitOnCreateConfirmation）。
			if (CurrentState == EDataStreamState::Invalid)
			{
				// Create stream
				// 本端被动创建：按 StreamIndex 反查 Definition 实例化对应 UDataStream。
				if (CreateStreamFromIndex(StreamIndex) != ECreateDataStreamResult::Success)
				{
					// If we fail, we set state as Invalid and send that to server.
					// 创建失败（如 ini 无对应 Definition）→ 写 Invalid 回去告诉对端"我拒绝"。
					SetStreamState(StreamIndex, EDataStreamState::Invalid);
				}
			}
			else if (CurrentState == EDataStreamState::WaitOnCreateConfirmation)
			{
				// Other side have now confirmed open
				// 对端确认创建（对端把 PendingCreate 当作 Open 的同义） → 本端转 Open。
				SetStreamState(StreamIndex, EDataStreamState::Open);
			}
			else
			{
				HandleUnexpectedState();
			}
		}
		break;

		case EDataStreamState::Open:
		{
			// Open is received when other side has accepted stream
			if (CurrentState == EDataStreamState::WaitOnCreateConfirmation)
			{
				// We have now completed open handshake and can send data.
				SetStreamState(StreamIndex, EDataStreamState::Open);
			}
			else if (CurrentState == EDataStreamState::Open)
			{
				// We are already open, nothing to do
				// 双方都 Open，幂等。
			}
			else
			{
				HandleUnexpectedState();
			}
		}
		break;
		
		case EDataStreamState::PendingClose:
		{						
			if (CurrentState == EDataStreamState::PendingCreate)
			{
				// Received Pending close while we have yet to acknowledge or sent create
				// 罕见竞态：本端尚未发出创建确认，对端就发来关闭。先把状态推到 WaitOnCreateConfirmation 再到 PendingClose，保持状态机连续。
				SetStreamState(StreamIndex, EDataStreamState::WaitOnCreateConfirmation);
				SetStreamState(StreamIndex, EDataStreamState::PendingClose);
			} 
			else if (CurrentState == EDataStreamState::WaitOnCreateConfirmation || CurrentState == EDataStreamState::Open)
			{
				// Pending close is received when other side has started to close the connection
				// there might still be data to be flushed but no new data should be written
				// 对端发起关闭，本端进入 PendingClose（仍可 flush reliable，但不写新业务数据）。
				SetStreamState(StreamIndex, EDataStreamState::PendingClose);
			}
			else if (CurrentState == EDataStreamState::PendingClose)
			{
				UDataStream* Stream = Streams[StreamIndex];
				if (Stream->HasAcknowledgedAllReliableData())
				{
					// 双方都在 PendingClose，且本端 reliable 已全 ACK → 推进 WaitOnCloseConfirmation。
					SetStreamState(StreamIndex, EDataStreamState::WaitOnCloseConfirmation);
				}
				else
				{
					// 还在 flush，仅 dirty 让下次 WriteData 重写状态。
					UE_LOG_DATASTREAM_CONN(Verbose, TEXT("Flushing DataStream StreamIndex: %u in State: %s"), StreamIndex, LexToString(CurrentState));
					MarkStreamStateDirty(StreamIndex);
				}
			}
			else if (CurrentState == EDataStreamState::WaitOnCloseConfirmation)
			{
				// Trigger update of state machine as other side might still be flushing
				// 对端可能还在 flush，本端已等待。dirty 重写状态以让对端推进。
				MarkStreamStateDirty(StreamIndex);				
			}
			else
			{
				HandleUnexpectedState();
			}
		}
		break;

		case EDataStreamState::WaitOnCloseConfirmation:
		{
			if (CurrentState == EDataStreamState::WaitOnCloseConfirmation)
			{
				// 双方都进入 WaitOnCloseConfirmation —— 关闭握手完成 → 销毁。
				SetStreamState(StreamIndex, EDataStreamState::Invalid);
				DestroyStream(StreamIndex);
			}
			else if (CurrentState == EDataStreamState::PendingClose)
			{
				if (DataStream->HasAcknowledgedAllReliableData())
				{
					SetStreamState(StreamIndex, EDataStreamState::WaitOnCloseConfirmation);
				}
				else
				{
					// Trigger update of state machine as other side might still be flushing
					UE_LOG_DATASTREAM_CONN(Verbose, TEXT("Flushing DataStream StreamIndex: %u in State: %s"), StreamIndex, LexToString(CurrentState));
					MarkStreamStateDirty(StreamIndex);
				}
			}
			else
			{
				HandleUnexpectedState();
			}
		}
		break;

		case EDataStreamState::Invalid:
		{
			// Sent when a stream is invalidated
			if (CurrentState == EDataStreamState::Invalid)
			{
				// Do nothing
				// 双方都 Invalid，幂等。
			}
			else if (CurrentState == EDataStreamState::WaitOnCreateConfirmation)
			{
				// Report error and close stream
				// 对端拒绝创建 —— 本端走完整关闭流程后销毁。三连 SetStreamState 让状态机日志清晰留痕。
				SetStreamState(StreamIndex, EDataStreamState::PendingClose);
				SetStreamState(StreamIndex, EDataStreamState::WaitOnCloseConfirmation);

				SetStreamState(StreamIndex, EDataStreamState::Invalid);
				DestroyStream(StreamIndex);
			}
			else if (CurrentState == EDataStreamState::WaitOnCloseConfirmation)
			{
				// Ready to destroy
				// 对端确认关闭 → 直接销毁。
				SetStreamState(StreamIndex, EDataStreamState::Invalid);
				DestroyStream(StreamIndex);
			}
			else
			{
				HandleUnexpectedState();
			}
		}
		break;

		default:
			HandleUnexpectedState();
			break;
	}
}

/**
 * ReadData —— 主读包入口。
 *
 * Packet 头部布局（与 WriteData 镜像）：
 *   [5 bits]   StreamCount-1（读到后 +1 还原）
 *   [N bits]   DataStreamMask （N = StreamCount）
 *   [1 bit]    bHasDataStreamStateChanges
 *   [N bits]   [可选] DataStreamsWithChangedStateMask
 *   [4*K bits] [可选] 每条 dirty 流的状态值
 *   [...]      [可选] 每条 DataStreamMask 置位流的 substream payload
 *
 * 错误防护：
 *   - StreamCount > 实际本端流数 → 协议不一致，置 BitStreamError 直接返回。
 *   - DataStreamMask==0 且 !bHasDataStreamStateChanges → 整包没有任何信息，视为损坏。
 *   - 任一环节 HasErrorOrOverflow → 立即终止读取（避免后续读到错误数据）。
 */
void UDataStreamManager::FImpl::ReadData(UE::Net::FNetSerializationContext& Context)
{
	using namespace UE::Net;

	FNetBitStreamReader* Stream = Context.GetBitStreamReader();
	const uint32 StreamCount = 1U + Stream->ReadBits(StreamCountBitCount);
	const uint32 DataStreamMask = Stream->ReadBits(StreamCount);

	// Read and apply stream state changes
	const bool bHasDataStreamStateChanges = Stream->ReadBool();
	const uint32 DataStreamsWithChangedStateMask = bHasDataStreamStateChanges ? Stream->ReadBits(StreamCount) : 0U;

	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	// Validate the received information
	// 协议合法性校验：StreamCount 不能超过本端实际流数；至少要有 mask 或状态变更其一。
	if (StreamCount > uint32(Streams.Num()) || (DataStreamMask == 0U && !bHasDataStreamStateChanges))
	{
		Context.SetError(GNetError_BitStreamError);
		return;
	}

	TObjectPtr<UDataStream>* StreamData = Streams.GetData();

	if (bHasDataStreamStateChanges)
	{
		// 第一遍：读出所有状态变更并应用（HandleReceivedStreamState 内可能触发新流的实例化）。
		for (uint32 StreamIt = 0, StreamEndIt = StreamCount, Mask = 1U; StreamIt != StreamEndIt; ++StreamIt, Mask += Mask)
		{
			if (DataStreamsWithChangedStateMask & Mask)
			{
				// Read Stream state
				EDataStreamState RcvdState = (EDataStreamState)Stream->ReadBits(StreamStateBitCount);

				// If something went wrong we should stop deserializing immediately.
				if (Context.HasErrorOrOverflow())
				{
					return;
				}

				UE_LOG_DATASTREAM_CONN(Verbose, TEXT("ReadStreamState for StreamIndex: %u, State: %s"), StreamIt, LexToString(RcvdState));
				HandleReceivedStreamState(Context, StreamIt, RcvdState);
			}
		}
	}

	// 第二遍：按 DataStreamMask 逐条调用子流 ReadData。注意此时新创建的流（PendingCreate→对端被动创建）已就位。
	for (uint32 StreamIt = 0, StreamEndIt = StreamCount, Mask = 1U; StreamIt != StreamEndIt; ++StreamIt, Mask += Mask)
	{
		if (DataStreamMask & Mask)
		{
			// We should always have a DataStream here.
			UDataStream* DataStream = StreamData[StreamIt];
			if (ensure(DataStream))
			{
				DataStream->ReadData(Context);
			}
			// If something went wrong we should stop deserializing immediately.
			if (Context.HasErrorOrOverflow())
			{
				break;
			}
		}
	}
}

/**
 * ProcessPacketDeliveryStatus —— 把 packet 投递结果分发到各子流。
 *
 * 流程：
 *  ① Records.Peek() 取出队首 record（必须等于传入的 InRecord —— FIFO 严格保证）。
 *  ② NetExports.ProcessPacketDeliveryStatus(Status)：处理本包内 NetToken 导出的 ACK/Lost。
 *  ③ 遍历每个流位（DataStreamStateMask | DataStreamMask）：
 *      - 该流有"状态变更" + Status==Lost：基于当前状态做悲观推断，
 *        WaitOnCreateConfirmation → PendingCreate；WaitOnCloseConfirmation → PendingClose；
 *        无论命不命中分支，都 dirty 让下次 WriteData 重写状态。
 *      - 该流有 payload：把 Status 转发到子流 ProcessPacketDeliveryStatus（连带其 SubRecord）。
 *  ④ Records.Pop() 弹出已处理的 record（释放池中槽位）。
 */
void UDataStreamManager::FImpl::ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* InRecord)
{
	const FRecord* Record = Records.Peek();
	check(Record == InRecord);

	// Process delivery notifications for our NetExports
	NetExports.ProcessPacketDeliveryStatus(Status);

	// Forward the call to each DataStream that was included in the record.
	const uint32 DataStreamMask = Record->DataStreamMask;
	const uint32 DataStreamStateMask = Record->DataStreamStateMask;
	const uint32 StreamCount = Streams.Num();

	TObjectPtr<UDataStream>* StreamData = Streams.GetData();
	for (uint32 StreamIt = 0, StreamEndIt = StreamCount, Mask = 1U; StreamIt != StreamEndIt; ++StreamIt, Mask += Mask)
	{
		if (DataStreamStateMask & Mask)
		{
			// State transitions are driven explicitly, but if we drop a transition we must dirty the StreamState to trigger a write.			
			if  (Status == UE::Net::EPacketDeliveryStatus::Lost)
			{
				UE_LOG_DATASTREAM_CONN(Verbose, TEXT("ProcessPacketDeliveryStatus Handle Lost DataStream State for StreamIndex: %u CurrentState: %s "), StreamIt, LexToString(StreamState[StreamIt]));

				// Note: As we do not store actual lost state in record, we are pessimistic for pendingcreate/close
				// 因为没保存"包内具体状态值"，只能根据当前状态做悲观回滚：等确认状态 → 回退到对应 Pending 状态。
				if (StreamState[StreamIt] == EDataStreamState::WaitOnCreateConfirmation)
				{
					SetStreamState(StreamIt, EDataStreamState::PendingCreate);
				}
				else if (StreamState[StreamIt] == EDataStreamState::WaitOnCloseConfirmation)
				{
					SetStreamState(StreamIt, EDataStreamState::PendingClose);
				}
				// 不在以上两态时（例如已 Open / Invalid），单纯 dirty 重写状态即可。
				MarkStreamStateDirty(StreamIt);
			}
		}

		if (DataStreamMask & Mask)
		{
			UDataStream* DataStream = StreamData[StreamIt];
			// We should always have a DataStream here.
			if (ensure(DataStream))
			{
				DataStream->ProcessPacketDeliveryStatus(Status, Record->DataStreamRecords[StreamIt]);
			}
		}
	}

	Records.Pop();
}

// 聚合查询：所有有效子流都 HasAcknowledgedAllReliableData() == true 才返回 true。
// Manager 状态机 / 上层 NetDriver 用此判断是否可以安全关闭连接。
bool UDataStreamManager::FImpl::HasAcknowledgedAllReliableData() const
{
	for (TObjectPtr<const UDataStream> Stream : Streams)
	{
		if (Stream && !Stream->HasAcknowledgedAllReliableData())
		{
			return false;
		}
	}

	return true;
}

/**
 * CloseStream —— 公开 API，请求关闭一条 dynamic 流。
 *
 * 失败条件：
 *  - 名字未在 ini 注册（FindDefinition 返回 nullptr）。
 *  - 该流 bDynamicCreate=false（auto-create 流不可单独关闭，只能随连接销毁）。
 *  - Streams 数组里没有该名的对象（流尚未创建或已销毁）。
 *
 * 成功路径根据当前状态分流：
 *  - PendingCreate            → Invalid + DestroyStream（创建请求未发出 → 直接销毁）
 *  - WaitOnCreateConfirmation → PendingClose（仍要等握手完毕走完整流程）
 *  - Open                     → PendingClose
 *  - 其它（PendingClose / WaitOnCloseConfirmation / Invalid） → 不动作
 */
void UDataStreamManager::FImpl::CloseStream(const FName StreamName)
{
	// Find index
	const UDataStreamDefinitions* StreamDefinitions = GetDefault<UDataStreamDefinitions>();
	const FDataStreamDefinition* Definition = StreamDefinitions->FindDefinition(StreamName);

	if (!Definition)
	{
		UE_LOG_DATASTREAM_CONN(Warning, TEXT("UDataStreamManager::FImpl::CloseStream No DataStreamDefinition exists for name '%s' exists."), ToCStr(StreamName.GetPlainNameString()));
		return;
	}

	if (!Definition->bDynamicCreate)
	{
		UE_LOG_DATASTREAM_CONN(Warning, TEXT("UDataStreamManager::FImpl::CloseStream cannot request DataStream'%s' to be closed as it is not marked as bDynamicCreate."), ToCStr(StreamName.GetPlainNameString()));
		return;
	}

	if (!Streams.ContainsByPredicate(FindStreamByName(StreamName)))
	{
		UE_LOG_DATASTREAM_CONN(Warning, TEXT("UDataStreamManager::FImpl::CloseStream No DataStream with name '%s' exists."), ToCStr(StreamName.GetPlainNameString()));
		return;
	}

	const uint32 StreamIndex = StreamDefinitions->GetStreamIndex(*Definition);
	const EDataStreamState CurrentState = GetStreamState(StreamIndex);
	switch (CurrentState)
	{
		case EDataStreamState::PendingCreate:
		{
			// If we are in PendingCreate it means that we either have not yet sent create request and can go back to invalid and release the stream
			// 创建请求尚未发出 → 直接销毁，对端根本不知道有这条流。
			SetStreamState(StreamIndex, EDataStreamState::Invalid);
			DestroyStream(StreamIndex);
			break;
		}
		case EDataStreamState::WaitOnCreateConfirmation:
		{
			// If we are closed while waiting for create confirmation.
			// 已发出创建请求但未确认 —— 走标准关闭路径（后续走 PendingClose → WaitOnCloseConfirmation → Invalid）。
			SetStreamState(StreamIndex, EDataStreamState::PendingClose);
			break;
		}
		case EDataStreamState::Open:
		{
			SetStreamState(StreamIndex, EDataStreamState::PendingClose);
			break;
		}
		default:
		{
			// PendingClose / WaitOnCloseConfirmation / Invalid：已经在关闭路径上或已销毁，无需处理。
			break;
		}
	};
}

/**
 * CreateStreamFromDefinition —— 内部创建实现，按 Definition 分配 StreamIndex 槽位 + 实例化 UDataStream。
 *
 * Flags 路径：
 *  - None / 非 dynamic 流          ：实例化 + 立即 Open（auto-create 流的初始状态）。
 *  - RegisterIfStreamIsDynamic     ：若 bDynamicCreate=true 仅占位（Streams[i]=nullptr, State=Invalid），
 *                                    等运行期 OpenDataStream 真正创建。
 *  - 其它（None + dynamic）        ：实例化 + State=PendingCreate（启动握手，对端会被动创建）。
 *
 * 数组扩容：按需 SetNum 到 (StreamIndex+1)，已有项不变 —— 保证 5-bit 索引位顺序稳定。
 */
ECreateDataStreamResult UDataStreamManager::FImpl::CreateStreamFromDefinition(const FDataStreamDefinition& Definition, ECreateDataStreamFlags Flags)
{
	if (Definition.Class == nullptr)
	{
		// FixupDefinitions 时 StaticLoadClass 失败的兜底分支。
		return ECreateDataStreamResult::Error_InvalidDefinition;
	}

	const int32 WantedStreamIndex = UDataStreamDefinitions::GetStreamIndex(Definition);
	if (WantedStreamIndex == -1)
	{
		// FixupDefinitions 未跑（不应发生 —— InitStreams 会先 fixup）。
		return ECreateDataStreamResult::Error_InvalidDefinition;
	}

	// Bumping MaxStreamCount may require modifying the FRecord and WriteData/ReadData.
	// 5-bit 位掩码硬上限。提升此值需修改：FRecord::DataStreamMask 类型 + ReadBits/WriteBits 容量 + StreamCountBitCount。
	if (WantedStreamIndex >= MaxStreamCount)
	{
		return ECreateDataStreamResult::Error_TooManyStreams;
	}

	// Make room
	// 按需扩容：已有项保留，新增项默认初始化（Streams=nullptr, SendStatus=Pause(0), State=Invalid(0)）。
	const int32 RequiredStreamCount = WantedStreamIndex + 1;
	if (Streams.Num() < RequiredStreamCount)
	{
		Streams.SetNum(RequiredStreamCount, EAllowShrinking::No);
		StreamSendStatus.SetNumZeroed(RequiredStreamCount, EAllowShrinking::No);
		StreamState.SetNumZeroed(RequiredStreamCount, EAllowShrinking::No);
	}

	UDataStream* Stream = nullptr;

	bool bIsDynamic = Definition.bDynamicCreate;
	// 决定是否要"真正实例化 UObject"：
	//  - 非 dynamic 流：始终实例化（auto-create 路径）。
	//  - dynamic 流 + Flags 不含 RegisterIfStreamIsDynamic：实例化（运行期 OpenDataStream 路径）。
	//  - dynamic 流 + Flags 含 RegisterIfStreamIsDynamic：仅占位（连接 Init 时的预登记路径）。
	bool bShouldCreate = !bIsDynamic || !EnumHasAnyFlags(Flags, ECreateDataStreamFlags::RegisterIfStreamIsDynamic);
	if (bShouldCreate)
	{
		// MakeUniqueObjectName：确保多个相同 Definition 但不同名的实例（罕见）不冲突。
		Stream = NewObject<UDataStream>(GetTransientPackage(), ToRawPtr(Definition.Class), MakeUniqueObjectName(nullptr, Definition.Class, Definition.DataStreamName));

		Streams[WantedStreamIndex] = Stream;
		StreamSendStatus[WantedStreamIndex] = Definition.DefaultSendStatus;

		// Auto created streams are always considered to be opened
		// 关键差异：
		//   - dynamic 流：从 PendingCreate 起步（要走握手）。
		//   - 非 dynamic 流（auto-create）：直接 Open（双方都已隐式约定该流必然存在，无需握手）。
		StreamState[WantedStreamIndex] = bIsDynamic ? EDataStreamState::PendingCreate : EDataStreamState::Open;
		if (bIsDynamic)
		{
			// dynamic 流需要把 PendingCreate 写到下一帧 packet。非 dynamic 流不需要。
			MarkStreamStateDirty(WantedStreamIndex);
		}
		UE_LOG_DATASTREAM_CONN(Verbose, TEXT("Created DataStream with name '%s' with streamindex: %d State:%s"), ToCStr(Definition.DataStreamName.ToString()), WantedStreamIndex, LexToString(StreamState[WantedStreamIndex]));
	}
	else
	{
		// 仅占位 —— 对端 dirty 来时本端会通过 HandleReceivedStreamState::CreateStreamFromIndex 真正创建。
		Streams[WantedStreamIndex] = nullptr;
		StreamSendStatus[WantedStreamIndex] = EDataStreamSendStatus::Pause;
		StreamState[WantedStreamIndex] = EDataStreamState::Invalid;

		UE_LOG_DATASTREAM_CONN(Verbose, TEXT("Registered DataStream with name '%s' with streamindex: %d State:%s"), ToCStr(Definition.DataStreamName.ToString()), WantedStreamIndex, LexToString(StreamState[WantedStreamIndex]));
	}

	// Init stream
	// 仅当 Stream != nullptr 时 InitStream 才会真正调用 Init（IsValid 检查）。
	InitStream(Stream, Definition.DataStreamName);

	return ECreateDataStreamResult::Success;
}

/**
 * 公开 CreateStream 入口（按名）：先查重，再按 ini 取 Definition 分发到 CreateStreamFromDefinition。
 *
 * 重要：调用方必须保证收发两端**对同一个 StreamName 调用顺序一致**，否则握手时 StreamIndex 路由会乱。
 */
ECreateDataStreamResult UDataStreamManager::FImpl::CreateStream(const FName StreamName, ECreateDataStreamFlags Flags)
{
	if (Streams.ContainsByPredicate(FindStreamByName(StreamName)))
	{
		UE_LOG_DATASTREAM_CONN(Warning, TEXT("A DataStream with name '%s' already exists."), ToCStr(StreamName.GetPlainNameString()));
		return ECreateDataStreamResult::Error_Duplicate;
	}
	
	const UDataStreamDefinitions* StreamDefinitions = GetDefault<UDataStreamDefinitions>();
	if (const FDataStreamDefinition* Definition = StreamDefinitions->FindDefinition(StreamName))
	{
		return CreateStreamFromDefinition(*Definition, Flags);
	}

	return ECreateDataStreamResult::Error_MissingDefinition;
}

/**
 * 按 StreamIndex 创建（被动创建路径）。
 *
 * 调用时机：HandleReceivedStreamState 中收到对端 PendingCreate 而本端 CurrentState=Invalid —— 即"对端发起、本端镜像"。
 * 用 StreamIndex 而不是 Name 是因为 packet 中只携带 5-bit 索引（节省带宽）。
 *
 * 失败条件：
 *  - FixupDefinitions 未跑（罕见，理论上 InitStreams 会先 fixup）。
 *  - 找不到对应 StreamIndex 的 Definition（对端用了本端不认识的索引 —— ini 不一致）。
 */
ECreateDataStreamResult UDataStreamManager::FImpl::CreateStreamFromIndex(int32 StreamIndex)
{	
	const UDataStreamDefinitions* StreamDefinitions = GetDefault<UDataStreamDefinitions>();
	if (!StreamDefinitions->bFixupComplete)
	{
		UE_LOG_DATASTREAM_CONN(Warning, TEXT("Cannot create datastream by index if DataStreamDefinitions are not FixedUp."));
		return ECreateDataStreamResult::Error_MissingDefinition;
	}

	if (const FDataStreamDefinition* Definition = StreamDefinitions->FindDefinition(StreamIndex))
	{
		// Flags=None：被动创建路径必须实例化 UObject（即使 bDynamicCreate=true）。
		return CreateStreamFromDefinition(*Definition, ECreateDataStreamFlags::None);
	}

	return ECreateDataStreamResult::Error_MissingDefinition;
}

// 按名返回 const 子流指针。
inline const UDataStream* UDataStreamManager::FImpl::GetStream(const FName StreamName) const
{
	const TObjectPtr<UDataStream>* Stream = Streams.FindByPredicate(FindStreamByName(StreamName));
	return Stream != nullptr ? *Stream : nullptr;
}

// 按名返回可变子流指针。
inline UDataStream* UDataStreamManager::FImpl::GetStream(const FName StreamName)
{
	TObjectPtr<UDataStream>* Stream = Streams.FindByPredicate(FindStreamByName(StreamName));
	return Stream != nullptr ? *Stream : nullptr;
}

// 设置子流的发送状态。流不存在时 Display 级日志（非 Warning，因为可能是合法的"流尚未握手完成"）。
void UDataStreamManager::FImpl::SetSendStatus(const FName StreamName, EDataStreamSendStatus Status)
{
	const int32 Index = Streams.IndexOfByPredicate(FindStreamByName(StreamName));
	if (Index == INDEX_NONE)
	{
		UE_LOG_DATASTREAM_CONN(Display, TEXT("Cannot set send status for DataStream '%s' that hasn't been created."), ToCStr(StreamName.GetPlainNameString()));
		return;
	}

	StreamSendStatus[Index] = Status;
}

// 查询子流的发送状态。流不存在时返回防御默认 Pause。
EDataStreamSendStatus UDataStreamManager::FImpl::GetSendStatus(const FName StreamName) const
{
	const int32 Index = Streams.IndexOfByPredicate(FindStreamByName(StreamName));
	if (Index == INDEX_NONE)
	{
		UE_LOG_DATASTREAM_CONN(Display, TEXT("Cannot retrieve send status for DataStream '%s' that hasn't been created. Returning Pause."), ToCStr(StreamName.GetPlainNameString()));
		return EDataStreamSendStatus::Pause;
	}

	return StreamSendStatus[Index];
}

// 公开 GetStreamState（按名）：未找到返回 Invalid。
UDataStream::EDataStreamState UDataStreamManager::FImpl::GetStreamState(const FName StreamName) const
{
	const int32 Index = Streams.IndexOfByPredicate(FindStreamByName(StreamName));
	if (Index == INDEX_NONE)
	{
		return UDataStream::EDataStreamState::Invalid;
	}
	return GetStreamState(Index);
}

/**
 * 初始化 record 内存池 + 循环队列。
 *
 * 设计要点：
 *  - RecordStorage 是固定容量数组（容量 = PacketWindowSize），永远不重新分配。
 *  - Records 是循环队列，存的是 FRecord*（指向 RecordStorage 的元素）。
 *  - 队列满 / 空状态由 Records.Count() 判定，与 RecordStorage 的实际占用无关。
 *  - 初始化技巧：先把所有指针 Enqueue 进 Records（建立指针 → 元素的稳定映射），再 Reset 让 Count 归零，
 *    后续 Enqueue_GetRef 会按 FIFO 顺序循环复用这些指针指向的存储。
 *  - "TResizableCircularQueue 对 POD 类型不会修改其内容" —— Reset 不会清空指针。
 */
void UDataStreamManager::FImpl::InitRecordStorage()
{
	const uint32 PacketWindowSize = InitParameters.PacketWindowSize;
	RecordStorage.SetNum(PacketWindowSize);

	Records = TResizableCircularQueue<FRecord*>(PacketWindowSize);
	for (uint32 It = 0, EndIt = PacketWindowSize; It != EndIt; ++It)
	{
		FRecord*& Record = Records.Enqueue();
		Record = &RecordStorage[It];
	}

	// Note: The circular queue will not modify the contents of its storage for POD types.
	Records.Reset();
}

/**
 * 单条流的 Init：构造 per-stream 的 FInitParameters（继承 Manager 的 + 流名）并调用 Stream->Init。
 *
 * 契约校验：调用 Init 后子流的 GetDataStreamName() 必须返回传入的 DataStreamName，
 * 否则说明派生类 override 了 Init 但没调用 Super::Init —— 这会导致后续 GetState/RequestClose 失效。
 */
void UDataStreamManager::FImpl::InitStream(UDataStream* Stream, FName DataStreamName)
{
	if (IsValid(Stream))
	{
		UDataStream::FInitParameters StreamInitParameters(InitParameters);
		StreamInitParameters.Name = DataStreamName;

		Stream->Init(StreamInitParameters);

		// Catch if DataStream does not call Super::Init.
		ensureMsgf(Stream->GetDataStreamName() == DataStreamName, TEXT("DataStream %s did not call Super::Init"), *DataStreamName.ToString());
	}
}

/**
 * 连接 Init 时一次性按 ini 注册所有 auto-create / dynamic 流。
 *
 * 步骤：
 *  ① FixupDefinitions（若首次）—— StaticLoadClass + 分配 StreamIndex。
 *  ② 收集所有 bAutoCreate || bDynamicCreate 的流名。
 *  ③ 逐个 CreateStream(name, RegisterIfStreamIsDynamic)：
 *      - bAutoCreate=true 流 → 实际实例化 + State=Open。
 *      - bDynamicCreate=true 流 → 仅占位（StreamIndex 锁定，State=Invalid，待运行期 OpenDataStream）。
 */
void UDataStreamManager::FImpl::InitStreams()
{
	UDataStreamDefinitions* StreamDefinitions = GetMutableDefault<UDataStreamDefinitions>();
	StreamDefinitions->FixupDefinitions();

	TArray<FName> StreamsToAutoCreateOrRegister;
	StreamsToAutoCreateOrRegister.Reserve(MaxStreamCount);
	StreamDefinitions->GetStreamNamesToAutoCreateOrRegister(StreamsToAutoCreateOrRegister);

	for (const FName& StreamName : StreamsToAutoCreateOrRegister)
	{
		// RegisterIfStreamIsDynamic：dynamic 流仅占位，等运行期 OpenDataStream 真正实例化。
		CreateStream(StreamName, ECreateDataStreamFlags::RegisterIfStreamIsDynamic);
	}
}

// 暴露 Manager 级共享 NetExports。所有子流通过 InitParameters.NetExports 访问同一个对象。
UE::Net::Private::FNetExports& UDataStreamManager::FImpl::GetNetExports()
{
	return NetExports;
}

// 把 Streams 数组里的子流引用上报给 GC。Pimpl 内部数组对 GC 不可见，必须显式上报。
void UDataStreamManager::FImpl::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Streams);
}
