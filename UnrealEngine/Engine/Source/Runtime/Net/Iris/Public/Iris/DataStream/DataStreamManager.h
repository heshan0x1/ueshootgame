// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStreamManager.h —— 多路复用入口（每连接一条根 UDataStream）
// ---------------------------------------------------------------------------------------------
// `UDataStreamManager` 既是 `UDataStream` 的派生（**组合模式**），又是其它子流的容器：
//   - 对外（NetDriver / DataStreamChannel）只暴露这一条流；
//   - 内部聚合最多 32 条 `UDataStream` 子流，按 5-bit `StreamIndex` 索引；
//   - 在 packet 头部写入 `DataStreamMask + StateMask + 状态变更位` 来路由 / 还原子流数据；
//   - 提供 `CreateStream / GetStream / CloseStream / SetSendStatus / GetStreamState / IsKnownStreamDefinition`
//     等运行期管理 API；
//   - 通过 `GetNetExports()` 暴露统一的 `FNetExports`（NetToken / 对象引用导出表），各子流共享。
//
// 错误码：`ECreateDataStreamResult` 覆盖 5 种 CreateStream 失败原因。
//
// 实现采用 Pimpl（`TPimplPtr<FImpl>`），头文件保持精简；具体实现见 DataStreamManager.cpp。
// =============================================================================================

#pragma once

#include "CoreTypes.h"
#include "DataStream.h"
#include "Templates/PimplPtr.h"
#include "DataStreamManager.generated.h"

enum class EDataStreamSendStatus : uint8;
class UDataStream;

/**
 * CreateStream 返回值。
 *
 * 触发条件：
 *  - Success                    ：流被成功创建（auto / dynamic / 显式调用均可能命中）。
 *  - Error_Duplicate            ：同名流已存在 —— Streams 数组中已有以 StreamName 命名的对象。
 *  - Error_MissingDefinition    ：UDataStreamDefinitions（ini）中找不到该 StreamName 的定义。
 *  - Error_InvalidDefinition    ：找到了 Definition 但 Class==nullptr（StaticLoadClass 失败）
 *                                 或 StreamIndex==-1（FixupDefinitions 未跑）。
 *  - Error_TooManyStreams       ：StreamIndex >= MaxStreamCount（=32）；位掩码序列化容量已满。
 */
enum class ECreateDataStreamResult : uint8
{
	// DataStream was successfully created.
	Success,
	// A DataStream with that name is already created.
	Error_Duplicate,
	// There's no DataStreamDefinition for the requested DataStream.
	Error_MissingDefinition,
	// There's something wrong with the DataStreamDefinition for the requested DataStream.
	Error_InvalidDefinition,
	// There's a fixed limit on how many unique data streams can be created.
	Error_TooManyStreams,
};

namespace UE::Net::Private
{
	// 共享的 NetToken / 对象引用导出表，每条连接一份；由 Manager 持有，所有子流共用。
	class FNetExports;
}

/**
 * The DataStreamManager contains all active DataStreams that may serialize data.
 * Calls to the DataStream interface functions will be forwarded to active streams.
 * Which streams will be automatically created or allowed to be manually created
 * need to be configured via UDataStreamDefinitions.
 *
 * UDataStreamManager —— 一条 NetConnection 的"根数据流"，同时也是所有子流的容器/路由器。
 *
 * 自身继承 UDataStream，所以也实现 BeginWrite/WriteData/EndWrite/ReadData/ProcessPacketDeliveryStatus，
 * 但语义全是"分发到内部子流并聚合结果"。
 *
 * 关键 API：
 *  - 静态：`IsKnownStreamDefinition`（仅查 ini）
 *  - 实例（连接级）：CreateStream / GetStream / CloseStream / SetSendStatus / GetSendStatus / GetStreamState
 *
 * 可创建的流由 `UDataStreamDefinitions` ini 决定；运行期可通过
 * `UReplicationSystem::OpenDataStream(ConnId, Name)` / `CloseDataStream(...)`
 * 触发 dynamic 流的开关（最终落到 CreateStream / CloseStream）。
 */
UCLASS(transient, MinimalApi)
class UDataStreamManager : public UDataStream
{
	GENERATED_BODY()

public:
	IRISCORE_API virtual ~UDataStreamManager();

	// UDataStream interface
	// 以下 6 个虚函数都是"分发 + 聚合"语义：遍历每条子流（按 SendStatus / EDataStreamState 过滤）调用对应方法。

	/** Initializes the manager. No data stream can be created by the manager before this.
	 *  连接级初始化：分配 RecordStorage（按 PacketWindowSize），FixupDefinitions（首次），
	 *  按 ini 创建所有 bAutoCreate 流并占位所有 bDynamicCreate 流。
	 */
	virtual IRISCORE_API void Init(const FInitParameters& InitParams) override;

	/** Prepare for destruction. Data streams will get ProcessPacketDeliveryStatus for outstanding packets and then marked as garbage.
	 *  连接级反初始化：先用 EPacketDeliveryStatus::Discard 把 Records 队列里所有未 ACK 的 record 派发掉
	 *  （让子流释放资源），然后 Deinit + MarkAsGarbage 每条子流。
	 *  注意：本方法不是 override —— UDataStream 基类的 Deinit 是普通虚函数；这里同名隐藏。
	 */
	IRISCORE_API void Deinit();

	IRISCORE_API virtual void Update(const FUpdateParameters& Params) override;

	/** Call BeginWrite on all active data streams.
	 *  对所有 SendStatus!=Pause 的子流调用 BeginWrite，聚合返回值（任一 HasMoreData → 整体 HasMoreData）。
	 *  返回 NoData 表示当前帧 + DirtyStreamsMask==0 → 上层不再调用 WriteData/EndWrite。
	 */
	IRISCORE_API virtual EWriteResult BeginWrite(const FBeginWriteParameters& Params) override;

	/** Call WriteData on all active data streams.
	 *  打 packet 主入口：先写 Manager header（DataStreamMask + StateMask + 状态变更位），
	 *  再为每条 Open / PendingClose 的子流创建 substream 调用 WriteData，最后回填位掩码 + 入队 record。
	 *  详见 .cpp 实现。
	 */
	IRISCORE_API virtual EWriteResult WriteData(UE::Net::FNetSerializationContext& context, FDataStreamRecord const*& OutRecord) override;

	/** Call EndWrite on all active data streams.
	 *  与 BeginWrite 配对，对所有 SendStatus!=Pause 的子流调用 EndWrite。
	 */
	IRISCORE_API virtual void EndWrite() override;

	/** When a packet is received call ReadData on all data streams that wrote something.
	 *  按 packet 头位掩码反序列化：先读状态变更，再为每条 DataStreamMask 置 1 的子流调用 ReadData。
	 */
	IRISCORE_API virtual void ReadData(UE::Net::FNetSerializationContext& context) override;

	/** Called for all data streams that wrote to a packet whose delivery status is now known.
	 *  按 record 中保存的 mask 把 status 分发到各子流；并在 Lost 时把状态变更位回滚（PendingCreate/Close 重发）。
	 */
	IRISCORE_API virtual void ProcessPacketDeliveryStatus(UE::Net::EPacketDeliveryStatus Status, FDataStreamRecord const* Record) override;

	/** Returns true if calling HasAcknowledgedAllReliableData on all data streams also returns true.
	 *  聚合查询：所有子流都已 ACK 全部 reliable 数据时才返回 true。Manager 状态机用此决定何时由 PendingClose → WaitOnCloseConfirmation。
	 */
	IRISCORE_API virtual bool HasAcknowledgedAllReliableData() const override;

	// DataStreamManager specifics

	/**
	 * Returns true of the stream of with the specified name is present in the UDataStreamDefintions
	 *
	 * 静态 helper：仅检查 ini 中是否有该名定义（不依赖具体连接）。用于 NetDriver 在调用 CreateStream 前的预校验。
	*/
	IRISCORE_API static bool IsKnownStreamDefinition(const FName StreamName);

	/**
	 * Creates a DataStream that has been configured via UDataStreamDefinitions.
	 * @param StreamName The data stream name as configured in a FDataStreamDefinition.
	 *        Each stream needs a unique name, but there can be multiple streams of the same class
	 *        as long as each data stream name is unique.
	 * @return Success if the stream was successfully created or an error code if it was not.
	 * @see UDataStreamDefinitions
	 * @note Calls need to be synchronized between the sending and receiving side.
	 *
	 * 创建一条已在 ini 中注册的流。
	 *
	 * 重要约定：**调用必须在收发两端保持同步**。因为 StreamIndex 是 ini 行号 —— 两端必须用相同
	 * Definition 顺序、相同生效时机来 CreateStream，否则 packet 头部位掩码会路由到错误的流。
	 *
	 * 对于 bDynamicCreate=true 的流：典型流程是服务器先 CreateStream（PendingCreate），随
	 * 下一个 packet 把状态发给客户端，客户端在 ReadData 中收到 PendingCreate 后镜像 CreateStream
	 * 并回 ACK，最终双方进入 Open（详见 .cpp 中的握手代码）。
	 */
	IRISCORE_API ECreateDataStreamResult CreateStream(const FName StreamName);

	/**
	 * Gets the data stream with a given name.
	 * @param StreamName The name of the data stream as configured in UDataStreamDefinitions.
	 * @return A pointer to a const data stream if it exists, nullptr if not.
	 *
	 * 按名查找子流（const 版本）。线性扫描 Streams 数组（流量少，O(N) 可接受）。
	 */
	IRISCORE_API const UDataStream* GetStream(const FName StreamName) const;

	/**
	* Gets the data stream with a given name.
	* @param StreamName The name of the data stream as configured in UDataStreamDefinitions.
	* @return A pointer to the data stream if it exists, nullptr if not.
	*
	* 按名查找子流（可变版本）。
	*/
	IRISCORE_API UDataStream* GetStream(const FName StreamName);

	/**
	* Request that the dynamic data stream with the given name will be closed after handshake with remote peer. 
	* NOTE: This will not do anything for streams not marked as bDynamicCreate
	* @param StreamName The name of the data stream as configured in UDataStreamDefinitions.
	*
	* 请求关闭一条 dynamic 流。仅 bDynamicCreate=true 的流有效。
	* 内部根据当前状态走不同分支：
	*   PendingCreate            → Invalid（直接销毁，因为创建请求还未发出）
	*   WaitOnCreateConfirmation → PendingClose
	*   Open                     → PendingClose
	*/
	IRISCORE_API void CloseStream(const FName StreamName);

	/** Get the current state of a DataStream Returns Invalid if the stream isn't created.
	 *  返回 6 状态生命周期值；流不存在时返回 Invalid。
	 */
	IRISCORE_API EDataStreamState GetStreamState(const FName StreamName) const;

	/** Set the send status of an already created data stream.
	 *  运行期切换流的发送状态（Pause / Send）。流不存在时打 Display 日志并忽略。
	 */
	IRISCORE_API void SetSendStatus(const FName StreamName, EDataStreamSendStatus Status);

	/** Get the send status of an already created data stream. Returns Pause if the stream isn't created.
	 *  返回当前 SendStatus；流不存在时返回 Pause（防御默认）。
	 */
	IRISCORE_API EDataStreamSendStatus GetSendStatus(const FName StreamName) const;

	// 暴露连接级共享的 NetExports 对象 —— ReplicationDataStream 等会向它注册导出。
	UE::Net::Private::FNetExports& GetNetExports();

private:
	// 私有构造：仅 UObject 工厂可调用。CDO 路径不分配 Impl（HasAnyFlags(RF_ClassDefaultObject) 时跳过）。
	UDataStreamManager();

	// AddReferencedObjects 钩子：把内部 Streams 数组里的所有子流标记为 GC 引用根。
	// 由 .cpp 中静态注册到 UClass。Pimpl 内部 Streams 是 TArray<TObjectPtr<UDataStream>>，
	// 必须显式告诉 GC 才能阻止子流被回收。
	static void AddReferencedObjects(UObject* Object, FReferenceCollector& Collector);

private:
	// Pimpl：把所有内部状态（Streams / StreamSendStatus / StreamState / RecordStorage / NetExports / DirtyStreamsMask）
	// 隔离在 .cpp 里的 FImpl 中，使头文件保持极简。
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
