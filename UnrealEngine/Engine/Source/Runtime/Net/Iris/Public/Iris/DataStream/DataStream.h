// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStream.h —— Iris 中间件层"数据流"接口契约
// ---------------------------------------------------------------------------------------------
// 该文件定义了 Iris 复制框架中 **每连接** 的可插拔数据流抽象 `UDataStream`：
//   - 一条连接（NetConnection）持有一个根 `UDataStreamManager`（也是 `UDataStream` 的派生类，组合模式）。
//   - 在根 Manager 之下挂载若干条具体子流（如 `UReplicationDataStream`、`UNetTokenDataStream`、
//     `UChunkedDataStream`），每条子流通过 `UDataStreamDefinitions`（ini 配置）注册并按 5-bit 索引管理。
//   - 上层 NetDriver/DataStreamChannel 仅与根 Manager 交互，由 Manager 在 packet 头部写入位掩码后
//     再分发到每条子流，从而实现"多路复用 + 一致的 ACK/NACK 反馈"。
//
// 本头文件主要导出：
//   * `EDataStreamWriteMode`         —— 写模式（Full / PostTickDispatch）。
//   * `FDataStreamRecord`            —— 派生流实现自定义 ACK 跟踪结构的基类。
//   * `EDataStreamSendStatus`        —— 顶层 UENUM，控制流是否被允许写出（Pause / Send）。
//   * `UDataStream`                  —— 抽象基类：生命周期、写入管线、读取、投递反馈、状态机。
//     - `EWriteResult`               —— 写入返回三态（NoData / Ok / HasMoreData）。
//     - `EUpdateType`                —— 更新触发时机（PreSendUpdate / PostTickFlush）。
//     - `EDataStreamState`           —— 6 状态生命周期（Invalid → PendingCreate → … → Open → … → Invalid）。
//     - `FInitParameters / FBeginWriteParameters / FUpdateParameters` —— 各阶段参数封装。
// =============================================================================================

#pragma once

#include "CoreTypes.h"
#include "DataStream.generated.h"

// 前置声明：Manager 是 DataStream 的所有者/容器，DataStream 通过 Manager 反向调用 CloseStream/GetStreamState。
class UDataStreamManager;

namespace UE::Net
{
	// 序列化上下文：聚合 BitStream 读写器、错误信号、配额、各 Internal Context 等。
	class FNetSerializationContext;
	// 投递状态枚举：Delivered / Lost / Discard。来自 Iris/PacketControl 模块（最小契约层 L0）。
	enum class EPacketDeliveryStatus : uint8;

	namespace Private
	{
		// NetExports：每包级别的 NetToken/对象引用导出上下文，由 Manager 暴露给所有子流共享。
		class FNetExports;
	}


/**
 * 数据流写出模式。
 *
 * 由 `FBeginWriteParameters::WriteMode` 携带，提示子流当前帧应该写哪一类数据：
 *  - `Full`              ：默认值，允许写出全部数据（典型主复制管线）。
 *  - `PostTickDispatch`  ：只写"在 PostTickDispatch 之后才应发送的数据"。一般用于派发后才决定的
 *                          延后包（如某些 OOB attachments / SendImmediate RPC 路径）。
 */
enum class EDataStreamWriteMode : unsigned
{
	// Allowed to write all data, this is the default WriteMode
	// 允许写出全部数据；这是默认 WriteMode。
	Full,

	// Only write data that should be sent after PostTickDispatch
	// 仅写出"应该在 PostTickDispatch 之后才发送"的数据。
	PostTickDispatch,
};

}

/**
 * Base struct for data stream records which are returned with WriteData calls and provided to ProcessPacketDeliveryStatus calls.
 * It's up to each DataStream implementation to inherit, if needed, and store relevant information regarding what was written
 * in the packet so that when ProcessPacketDeliveryStatus is called the DataStream can act on it appropriately depending on
 * whether the packet was delivered or lost. The DataStream is responsible both for allocating and freeing its own records.
 *
 * 数据流投递记录基类。
 *
 * 每次 `WriteData` 写入数据成功（返回值非 `NoData`）时，子流可以"挂"一条 `FDataStreamRecord` 子类对象到
 * `OutRecord`；之后 packet 被对端 ACK / 丢失 / Discard 时，框架会以**与 WriteData 调用相同的顺序**回调
 * `ProcessPacketDeliveryStatus(Status, Record)` 把同一条记录交还，这样子流就可以：
 *   - Delivered：清掉对应 packet 内已确认发出的状态/changemask；
 *   - Lost     ：把丢失部分重新标脏以便下一次重发；
 *   - Discard  ：连接关闭时直接释放资源。
 *
 * 注意：
 *  1. **流自己分配、自己释放** —— UDataStream 负责 Record 的内存所有权。
 *  2. 该基类是空的 —— 仅作为多态根，派生类按需添加字段（如 changemask、SeqId、附件 list 等）。
 *  3. `UDataStreamManager` 内部的 `FRecord` 也继承自它，用来聚合一个 packet 内所有子流的 SubRecord。
 */
struct FDataStreamRecord
{
};

/**
 * Enum used to control whether a DataStream is allowed to write data or not.
 * As the DataStreamManager needs to know this the behavior is controlled there.
 * @see UDataStreamManager::GetSendStatus, UDataStreamManager::SetSendStatus
 *
 * 数据流发送状态。
 *
 * 控制某条子流"在当前帧是否被允许写出数据"。该状态由 `UDataStreamManager` 集中管理，是
 * `WriteData/BeginWrite/EndWrite` 主循环里的 short-circuit 条件：状态为 `Pause` 时直接 `continue`。
 *
 *  - `Pause = 0`  ：暂停写入（仍然可以接收 ReadData / 收到 ProcessPacketDeliveryStatus）。
 *  - `Send  = 1`  ：允许写入。
 *
 * 之所以放在顶层 UENUM（而非命名空间内），是为了便于 UObject/反射 / Blueprint 暴露。
 * 默认值由 `FDataStreamDefinition::DefaultSendStatus` 提供（ini 中 `DefaultSendStatus=Send`）。
 */
UENUM()
enum class EDataStreamSendStatus : uint8
{
	Pause = 0, // 暂停发送：BeginWrite/WriteData/EndWrite 都会跳过该流。
	Send,      // 允许发送。
};

/**
 * DataStream is an interface that facilitates implementing the replication of custom data, 
 * such as bulky data or data with special delivery guarantees.
 *
 * UDataStream —— Iris 数据流抽象基类（abstract / MinimalAPI / transient）。
 *
 * 派生使用方式：
 *  1) 继承本类 + 在 `[/Script/IrisCore.DataStreamDefinitions]` ini 注册一行配置（指定 ClassName / Auto/Dynamic）。
 *  2) 实现 `WriteData / ReadData / ProcessPacketDeliveryStatus / HasAcknowledgedAllReliableData`（PURE_VIRTUAL）。
 *  3) 可选覆写 `Init / Deinit / Update / BeginWrite / EndWrite`（基类已提供"空操作"默认实现）。
 *
 * 一条流的完整生命周期由 Manager 驱动：
 *   Init(FInitParameters) → [每帧 Update + BeginWrite/WriteData/EndWrite + ReadData + ProcessPacketDeliveryStatus] → Deinit()
 *
 * 状态机（由 Manager 驱动，详见 EDataStreamState）：
 *   Invalid → PendingCreate → WaitOnCreateConfirmation → Open → PendingClose → WaitOnCloseConfirmation → Invalid
 *
 * 注意 `transient` —— DataStream 不参与磁盘序列化，仅运行期存活。
 */
UCLASS(abstract, MinimalAPI, transient)
class UDataStream : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * WriteData 返回值 —— 三态语义。
	 *
	 * 此返回值在 Manager 端会被聚合：只要任何一条子流返回 HasMoreData，Manager 也返回 HasMoreData，
	 * 通知上层"还可以写更多包"。
	 */
	enum class EWriteResult
	{
		// If NoData is returned then ReadData will not be called on the receiving end.
		// 没有写入任何数据；对端不会调用 ReadData，且**不会**产生 ACK 回调。
		NoData,
		// Everything was sent or this stream don't want to send more this frame even if there's more bandwidth.
		// 已写出（或本帧不再写出）；即便还有带宽预算，也不希望再被调用 WriteData。
		Ok, 
		// We have more data to write and can continue to write more if we get another call to write
		// 还有数据待写；如果带宽允许，希望被再次调用 WriteData 继续写出。
		HasMoreData,
	};

	/**
	 * Update 触发时机。
	 *
	 *  - PreSendUpdate ：来自 `UReplicationSystem::PreSendUpdate` —— 帧主流程的早期，做 dirty 收集 / 状态准备。
	 *  - PostTickFlush ：来自主网络 Tick 末尾 —— 包发送/派发结束后，做收尾、缓冲回收等。
	 */
	enum class EUpdateType : uint8
	{
		// Update originating from ReplicationSystem::PreSendUpdate
		// 来自 ReplicationSystem::PreSendUpdate（发送前更新）
		PreSendUpdate = 0,

		// Update originating from the end of the main Network tick
		// 来自主网络 Tick 末尾（发送完毕后的清理更新）
		PostTickFlush = 1,
	};

	/**
	 * 数据流生命周期状态机。
	 *
	 * 6 个状态 + 1 个 Count 哨兵；位宽 4-bit（足以表达，参见 .cpp 中的 static_assert）。
	 *
	 * 标准转移路径：
	 *   Invalid
	 *      └── 本端调用 CreateStream（dynamic）→ PendingCreate
	 *   PendingCreate
	 *      ├── WriteData 写出 PendingCreate 后 → WaitOnCreateConfirmation
	 *      └── 本端在写出前撤销 → Invalid（直接 DestroyStream）
	 *   WaitOnCreateConfirmation
	 *      ├── 收到对端 PendingCreate/Open → Open（握手完成）
	 *      ├── 对端拒绝（收到 Invalid）→ Invalid
	 *      ├── 收到对端 PendingClose（异常但允许）→ PendingClose
	 *      └── 投递丢失（Lost），状态被乐观回滚 → PendingCreate（重发）
	 *   Open
	 *      └── 本端 RequestClose / 收到对端 PendingClose → PendingClose
	 *   PendingClose
	 *      └── 所有 reliable 数据都已 ACK → WaitOnCloseConfirmation
	 *   WaitOnCloseConfirmation
	 *      ├── 收到对端 WaitOnCloseConfirmation 确认 → Invalid（销毁）
	 *      └── 投递丢失，状态被回滚 → PendingClose（重发）
	 *
	 * 详见 DataStreamManager.cpp::FImpl::SetStreamState / HandleReceivedStreamState。
	 */
	enum class EDataStreamState : uint8
	{
		// Stream is invalid
		// 流无效（默认/已关闭/未创建）。
		Invalid = 0,
		// We should send open/init to other side
		// 本端已请求创建，等待下一次 WriteData 把"创建请求"写到包里发送给对端。
		PendingCreate, 
		// We are waiting for confirmation that remote have accepted the stream
		// 创建请求已发出，等待对端确认（对端会发回 PendingCreate/Open 表示接受）。
		WaitOnCreateConfirmation,
		// Stream is open and we will process incoming data.
		// 已开通：双向都可以收发。
		Open,
		// We are closing, but still considerd open until flushed
		// 本端发起关闭：仍可继续 flush 已 reliable 数据，但不再写新的业务数据。
		PendingClose,
		// We have send a close request and is waiting for confirmation before invalidating stream
		// 关闭请求已发出，等待对端确认；对端确认后才真正销毁。
		WaitOnCloseConfirmation,

		Count // 状态总数哨兵；用于编译期断言 EDataStreamState 是否能装入 4-bit。
	};

	/**
	 * Init 阶段参数。
	 *
	 * 由 Manager 在 `Init()` 中构造并传给每条子流；包含连接级共享的运行期信息。
	 * 注意私有构造重载用于 Manager 把 `DataStreamManager` 指针注入子流（友元访问），
	 * 子流通过 `RequestClose/GetState` 反向调用回 Manager。
	 */
	struct FInitParameters
	{
	public:
		FInitParameters() = default;
		// 拷贝 InParams 的所有公开字段，并强制把 DataStreamManager 设为 InDataStreamManager。
		// 仅由 Manager 在调用子流 Init 之前使用。
		FInitParameters(UDataStreamManager* InDataStreamManager, const FInitParameters& InParams)
		{
			*this = InParams;
			DataStreamManager = InDataStreamManager;
		}

		UE::Net::Private::FNetExports* NetExports = nullptr; // 共享的导出上下文（NetToken / 对象引用），由 Manager 持有并下发。
		FName Name;                                          // 当前流的注册名（与 FDataStreamDefinition::DataStreamName 对应）。
		uint32 ReplicationSystemId = 0U;                     // 所在 UReplicationSystem 的 Id（多 PIE 实例区分）。
		uint32 ConnectionId = 0U;                            // 所在 NetConnection 的 Id。
		uint32 PacketWindowSize = 0U;                        // 包窗口大小，决定 Manager 端 RecordStorage 容量（最大未 ACK 包数）。

	private:
		// We only want this to be accessible from UDataStream base
		// 只允许 UDataStream 基类访问 —— 子流不应直接拿到 Manager 指针，必须通过基类提供的 RequestClose/GetState 等方法。
		UDataStreamManager* DataStreamManager = nullptr;
		friend UDataStream;
	};

	/**
	 * BeginWrite 阶段参数。
	 *
	 * Manager 把同一份参数原样转发给每条子流的 `BeginWrite`，子流可据此提前准备
	 * "可在多次 WriteData 间保持的数据"（如 ChangeMask 切片、批次抬头等）。
	 */
	struct FBeginWriteParameters
	{
		UE::Net::EDataStreamWriteMode WriteMode = UE::Net::EDataStreamWriteMode::Full; // 写模式：默认 Full。

		// Default to sending 1 packet per write. If 0 = unlimited packets
		// 单次 BeginWrite 之后允许写入的最大包数；0 表示不限。默认每次 BeginWrite 对应 1 个 packet。
		uint32 MaxPackets = 1U;
	};

	/**
	 * Update 阶段参数 —— 仅携带触发时机（PreSendUpdate / PostTickFlush）。
	 */
	struct FUpdateParameters
	{
		EUpdateType UpdateType;
	};

public:
	IRISCORE_API virtual ~UDataStream();

	/** Called before any other calls are made.
	 *  在 DataStream 被首次使用前调用，由 Manager 触发。基类默认实现仅缓存 Params 到 DataStreamInitParameters。
	 *  派生类应当 `Super::Init(Params)` 之后再做自己的初始化（否则 GetDataStreamName / GetState 等基础 API 会失效）。
	 */
	IRISCORE_API virtual void Init(const FInitParameters& Params);

	/** Called when a created stream is destroyed.
	 *  当流被 Manager 销毁前调用（`DestroyStream` / `Deinit`）。基类默认 no-op；派生类用于释放派生资源。
	 */
	IRISCORE_API virtual void Deinit();

	/** 
	 * Called to drive required updates during the ReplicationSystem update calls.
	 * 每帧驱动派生流推进自己的内部状态机；由 Manager 在 ReplicationSystem 的 `PreSendUpdate` /
	 * `PostTickFlush` 阶段触发，触发时机以 `Params.UpdateType` 区分。
 	 */
	IRISCORE_API virtual void Update(const FUpdateParameters& Params);

	/**
	 * Called before any calls to potential WriteData, if it returns EWriteData::NoData no other calls will be made.
	 * The purpose of the method is to enable a DataStream to setup data that can persist over multiple calls to WriteData if bandwidth allows.
	 *
	 * 在本帧首次 WriteData 之前调用。如果返回 `NoData`，本帧后续不再调用任何 Write 系列方法（含 EndWrite）。
	 * 用途：派生流可在此预先收集"跨多次 WriteData 共享"的数据（例如：本帧打算写出的所有对象列表、全局 header 等）。
	 *
	 * 基类默认实现返回 `HasMoreData`（保守地认为"还有数据要写"），见 DataStream.cpp。
	*/
	IRISCORE_API virtual EWriteResult BeginWrite(const FBeginWriteParameters& Params);

	/**
	 * Serialize data to a bitstream and optionally store record of what was serialized to a custom FDataStreamRecord.
	 * The FDataStreamRecord allow streams to implement custom delivery guarantees as they see fit by using the stored
	 * information when ProcessPacketDeliveryStatus is called. For each WriteData call returning something other than NoData
	 * a corresponding call to ProcessPacketDeliveryStatus will be made.
	 * The UDataStream owns the FDataStreamRecord, but there will always be a call to ProcessPacketDeliveryStatus passing
	 * the original OutRecord so that it can be deleted when all packets have been ACKed/NAKed or when the owning connection is closed.
	 * @param Context The FNetSerializationContext which has accesssors for the bitstream to write to among other things.
	 * @param OutRecord Set the data stream specific record to OutRecord so that it can be passed in a future ProcessPacketDeliveryStatus call.
	 *        ProcessPacketDeliveryStatus will be called with the record in the same order as WriteData was called.
	 * @return Whether there was data written or not and if the stream has more data to write if bandwidth settings allow it.
	 *
	 * 写出主入口（PURE_VIRTUAL，必须实现）。
	 *
	 * 契约：
	 *   - 把数据序列化进 `Context.GetBitStreamWriter()`。
	 *   - 若返回非 `NoData`，**必须**通过 `OutRecord` 指针返回一条派生 record，用于后续 ACK 回调；
	 *     若返回 `NoData` 或写入过程中 `SubContext.HasError()`，则 `OutRecord` 必须保持 `nullptr`
	 *     （Manager 在 .cpp 里有 checkf / ensureMsgf 强校验）。
	 *   - record 内存由派生流自己管理（自分配自释放）；Manager 仅原样回传给 ProcessPacketDeliveryStatus。
	 *   - ProcessPacketDeliveryStatus 的回调顺序 = WriteData 的写入顺序（FIFO）。
	 *   - 写入失败（substream overflow / 显式 Error）时 Manager 会丢弃 substream，相当于该流本帧没写。
	 */
	virtual EWriteResult WriteData(UE::Net::FNetSerializationContext& Context, FDataStreamRecord const*& OutRecord) PURE_VIRTUAL(WriteData, return EWriteResult::NoData;);

	/**
	 * Called after the final call to WriteData this frame, allowing the DataStream to cleanup data setup during BeginWrite.
	 *
	 * 本帧最后一次 WriteData 之后调用，与 BeginWrite 配对。派生流可在此释放跨 WriteData 调用持有的临时资源。
	 * 基类默认 no-op。
	 */
	IRISCORE_API virtual void EndWrite();

	/**
	 * Deserialize data that was written with WriteData.
	 * @param Context The FNetSerializationContext which has accessors for the bitstream to read from among other things. 
	 *
	 * 读取主入口（PURE_VIRTUAL，必须实现）。
	 *
	 * 由 Manager 在 ReadData 主循环中按 DataStreamMask 分发：只有曾经在对应位被置 1 的子流才会被 ReadData。
	 * 派生流应当在此读出与 WriteData 配对的字节，必要时立即 apply（如调度内部消息处理）。
	 */
	virtual void ReadData(UE::Net::FNetSerializationContext& Context) PURE_VIRTUAL(ReadData,);

	/**
	 * For each packet into which we have written data we are guaranteed to get a call to ProcessPacketDeliveryStatus when
	 * it's known whether the packet was delivered or not.
	 * @param Status Whether the packet was delivered or not or if the record should simply be discarded due to closing a connection.
	 * @param Record The record which was set by this stream during a WriteData call.
	 *
	 * 投递反馈回调（PURE_VIRTUAL）。
	 *
	 * Status 三态：
	 *  - Delivered ：包被对端确认；派生流可释放 record + 清理本包对应的 dirty/重传缓冲。
	 *  - Lost      ：包丢失；派生流应将本包内容重新标脏，等待下一次 WriteData 时重发。
	 *  - Discard   ：连接关闭强制丢弃；派生流仅释放 record，无需重发。
	 *
	 * 与 WriteData 严格 1:1 配对，按 FIFO 顺序回调。
	 */
	virtual void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record) PURE_VIRTUAL(ProcessPacketDeliveryStatus,);

	/**
	 * @return true if the stream has no pending reliable data for which it is waiting on an acknowledgement.
	 *
	 * 是否所有 reliable 数据都已被对端 ACK。
	 *
	 * Manager 用此返回值决定：
	 *  - PendingClose → 是否可立即转 WaitOnCloseConfirmation（需要 reliable 全部 ACK 才能关闭）。
	 *  - HasAcknowledgedAllReliableData()：聚合所有子流，所有都 true 才返回 true。
	 */
	virtual bool HasAcknowledgedAllReliableData() const PURE_VIRTUAL(HasAcknowledgedAllReliableData, return true;);

	/**
	 * Get name of DataStream
	 * 返回流的注册名（即 FDataStreamDefinition::DataStreamName）。基类 Init 会缓存到 DataStreamInitParameters.Name。
	 */
	FName GetDataStreamName() const
	{
		return DataStreamInitParameters.Name;
	}

	/**
 	 * Initiate close of DataStream. Note: This only applies to DataStreams that are flagged with bDynamicCreate in the DataStreamDefinition
	 * @param Name The name of the DataStream. Names of valid DataStream are configured in UDataStreamDefinitions.
	 * @see UDataStreamDefinitions
	 *
	 * 由派生流向 Manager 发起"关闭本流"的请求。
	 * 仅对 `bDynamicCreate=true` 的流有效；非 dynamic 流（auto-created）由 Manager 在 Deinit 时统一销毁。
	 *
	 * 内部实现转发到 `DataStreamManager->CloseStream(GetDataStreamName())`，由 Manager 的状态机处理：
	 *   PendingCreate            → Invalid（直接销毁，未发出过创建请求）
	 *   WaitOnCreateConfirmation → PendingClose
	 *   Open                     → PendingClose
	*/
	IRISCORE_API void RequestClose();

	/** Get the current state of the DataStream.
	 *  返回当前流的 6 状态生命周期值；通过 Manager 反查（GetStreamState）。
	 *  若 DataStreamManager 尚未注入（极少见，仅在裸构造未 Init 的情况下），返回 Invalid。
	 */
	IRISCORE_API const UDataStream::EDataStreamState GetState() const;

protected:
	/** Access init parameters.
	 *  受保护访问器：派生流可在 Init 之后任何时刻读取自己被 Manager 注入的连接级运行期信息。
	 */
	const UDataStream::FInitParameters& GetInitParameters() const
	{
		return DataStreamInitParameters;
	}

private:
	// 缓存 Manager 在 Init 中注入的参数（含 Name / NetExports / ReplicationSystemId / ConnectionId / PacketWindowSize / DataStreamManager 指针）。
	FInitParameters DataStreamInitParameters;
};

// 全局自由函数：把 EDataStreamState 转换成可读字符串（用于日志）。实现见 DataStream.cpp。
const TCHAR* LexToString(const UDataStream::EDataStreamState State);
