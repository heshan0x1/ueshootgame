// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStream.cpp —— UDataStream 抽象基类的"空操作"默认实现 + 状态字符串化 + 转发到 Manager
// ---------------------------------------------------------------------------------------------
// 本文件内容很短：
//   * 析构 / Init / Deinit / Update / BeginWrite / EndWrite 的基类默认实现 —— 多为 no-op。
//     Init 是唯一存有副作用的实现：缓存参数到 DataStreamInitParameters。
//   * LexToString(EDataStreamState) —— 状态枚举到日志字符串映射，含 static_assert 长度校验。
//   * GetState / RequestClose —— 转发到 DataStreamManager 的薄包装。
//
// 注意：WriteData / ReadData / ProcessPacketDeliveryStatus / HasAcknowledgedAllReliableData
// 在头文件中是 PURE_VIRTUAL，本文件不提供实现 —— 派生类必须各自实现。
// =============================================================================================

#include "Iris/DataStream/DataStream.h"
#include "Iris/DataStream/DataStreamManager.h"

// UHT 生成的 *.gen.cpp 内联挂入本翻译单元，避免单独 .gen.cpp 编译。
#include UE_INLINE_GENERATED_CPP_BY_NAME(DataStream)

// 析构：基类无可释放资源，定义在 .cpp 是为了把 vtable 钉在 IrisCore.dll（避免每个 TU 各自合成 vtable）。
UDataStream::~UDataStream()
{
}

// 默认 BeginWrite：保守地返回 HasMoreData，意为"我可能还想写"，让 Manager 不会因为基类返回 NoData 而短路掉派生流。
// 派生流应当根据自身 dirty 状态返回更精确的结果（NoData 时 Manager 会跳过本帧 WriteData/EndWrite）。
UDataStream::EWriteResult UDataStream::BeginWrite(const FBeginWriteParameters& Params)
{
	return EWriteResult::HasMoreData;
}

// 默认 EndWrite：no-op。派生流可在此释放跨多次 WriteData 持有的临时资源。
void UDataStream::EndWrite()
{
}

// 默认 Init：仅缓存参数。派生流必须 Super::Init(Params)，否则 GetDataStreamName / GetState / GetInitParameters 等都会拿到默认值
//（Manager 在 InitStream 末尾通过 ensureMsgf(Stream->GetDataStreamName()==Name) 检测此契约）。
void UDataStream::Init(const FInitParameters& Params)
{
	DataStreamInitParameters = Params;
}

// 默认 Deinit：no-op。派生流释放自有资源。
void UDataStream::Deinit()
{
}

// 默认 Update：no-op。派生流根据 Params.UpdateType（PreSendUpdate / PostTickFlush）做内部状态推进。
void UDataStream::Update(const FUpdateParameters& Params)
{
}

/**
 * EDataStreamState → 字符串（用于 UE_LOG 打印）。
 *
 * 编译期 static_assert 保证 Names[] 长度与枚举值数量一致 —— 给枚举添加新状态时编译就会失败，
 * 提醒同步本数组。运行期超出 Count 的非法值返回空串。
 */
const TCHAR* LexToString(const UDataStream::EDataStreamState State)
{
	static const TCHAR* Names[] = {
		TEXT("Invalid"),
		TEXT("PendingCreate"),
		TEXT("WaitOnCreateConfirmation"),
		TEXT("Open"),
		TEXT("PendingClose"),
		TEXT("WaitOnCloseConfirmation"),
	};
	static_assert(UE_ARRAY_COUNT(Names) == uint32(UDataStream::EDataStreamState::Count), "Missing names for one or more values of EDataStreamState.");

	return State < UDataStream::EDataStreamState::Count ? Names[(uint32)State] : TEXT("");
}

/**
 * 转发 GetState → DataStreamManager.GetStreamState(Name)。
 *
 * 派生流不持有自己的状态字段 —— 状态由 Manager 集中维护在 `FImpl::StreamState[StreamIndex]` 数组中。
 * 这样 Manager 可在握手 / Lost 等场景下原子地修改状态，无需通知子流。
 *
 * 边界条件：DataStreamManager 指针在 Init 之前为 nullptr，此时返回 Invalid（防御式）。
 */
const UDataStream::EDataStreamState UDataStream::GetState() const
{
	if (DataStreamInitParameters.DataStreamManager)
	{
		return DataStreamInitParameters.DataStreamManager->GetStreamState(GetDataStreamName());
	}
	else
	{
		return EDataStreamState::Invalid;
	}
}

/**
 * 转发 RequestClose → DataStreamManager.CloseStream(Name)。
 *
 * 仅对 bDynamicCreate=true 的流有效；非 dynamic 流由 Manager 在 Deinit 时统一销毁。
 * Manager 内部根据当前 EDataStreamState 决定走哪条转移路径（详见 FImpl::CloseStream）。
 */
void UDataStream::RequestClose()
{
	if (DataStreamInitParameters.DataStreamManager)
	{
		DataStreamInitParameters.DataStreamManager->CloseStream(GetDataStreamName());
	}
}
