// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PartialNetObjectAttachmentHandler.cpp —— 大附件分片 handler 实现
// -----------------------------------------------------------------------------
// 核心策略：
//   * "尽量不分片"——分片会显著增加协议开销（每片头/序号/重组缓冲）。
//   * 先用阈值 BitCountSplitThreshold 大小的固定缓冲做"探测序列化"：
//       - 未溢出：说明数据小于阈值 → 用 ShrinkWrap 包装一次性发出；
//       - 溢出  ：转父类 SplitNetBlob 重新走完整序列化 + 分片协议；
//   * Reliable 与 Unreliable 用不同阈值；Unreliable 还分 client/server 两套（带宽
//     特性差异）。
//   * Reliable 路径下，回滚依赖 FNetExportRollbackScope —— 探测失败时把已经触发
//     的 NetExport pending 列表回滚，避免污染。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/PartialNetObjectAttachmentHandler.h"
#include "Iris/ReplicationSystem/NetBlob/PartialNetBlob.h"
#include "Iris/ReplicationSystem/NetBlob/ShrinkWrapNetBlob.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetExportContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PartialNetObjectAttachmentHandler)

UPartialNetObjectAttachmentHandler::UPartialNetObjectAttachmentHandler()
: USequentialPartialNetBlobHandler()
{
}

UPartialNetObjectAttachmentHandler::~UPartialNetObjectAttachmentHandler()
{
}

void UPartialNetObjectAttachmentHandler::Init(const FPartialNetObjectAttachmentHandlerInitParams& InitParams)
{
	// 中文：阈值不能超过 65536（与底层位流缓冲编码上限挂钩）。
	ensure(InitParams.Config->GetBitCountSplitThreshold() <= 65536);

	FSequentialPartialNetBlobHandlerInitParams ParentInitParams = {};
	ParentInitParams.ReplicationSystem = InitParams.ReplicationSystem;
	ParentInitParams.Config = InitParams.Config;
	Super::Init(ParentInitParams);
}

bool UPartialNetObjectAttachmentHandler::PreSerializeAndSplitNetBlob(uint32 ConnectionId, const TRefCountPtr<UE::Net::FNetObjectAttachment>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, bool bSerializeWithObject)
{
	using namespace UE::Net;
	
	// 中文：阈值按 32 位字对齐（& ~31）—— 缓冲分配与 BitStreamWriter 都按 uint32 工作。
	uint32 BitCountSplitThreshold =  GetConfig()->GetBitCountSplitThreshold() & ~31U;	

	const FNetBlobCreationInfo& BlobCreationInfo = Blob->GetCreationInfo();
	const bool bIsReliable = EnumHasAnyFlags(BlobCreationInfo.Flags, ENetBlobFlags::Reliable);
	if (!bIsReliable)
	{
		// 中文：不可靠 → 按 Server/Client 阈值。Server 偏小（通常 256B）让 unreliable
		// 大包更主动分片；Client 偏大（850B）尽量整包发，避免分片丢失重组失败。
		BitCountSplitThreshold = (ReplicationSystem->IsServer() ? GetConfig()->GetServerUnreliableBitCountSplitThreshold() : GetConfig()->GetClientUnreliableBitCountSplitThreshold()) & ~31U;
	}

	TArray<uint32> Payload;
	Payload.AddUninitialized(BitCountSplitThreshold/32U);

	FNetBitStreamWriter Writer;
	Writer.InitBytes(Payload.GetData(), Payload.Num()*4U);

	// 中文：使用本 handler 私有的 ExportContext + NetExports，避免污染上层连接级状态。
	Private::FInternalNetSerializationContext InternalContext(ReplicationSystem);
	FNetSerializationContext SerializationContext(&Writer);
	// $IRIS TODO Cannot share serialization if there are serializers with connection specific serialization 
	SerializationContext.SetLocalConnectionId(ConnectionId);
	SerializationContext.SetInternalContext(&InternalContext);

	// Setup ExportScope to capture non object reference exports.
	// 中文：捕捉非引用类的 export（NetToken）—— 探测过程中产生的 pending 列表
	// 仍然有意义（即便走分片路径，token 也已被加入），故 ExportScope 不回滚。
	Private::FNetExportContext::FBatchExports BatchExports;
	Private::FNetExports::FExportScope ExportScope = NetExports.MakeExportScope(SerializationContext, BatchExports);

	{
		// 中文：FNetExportRollbackScope —— 仅在探测溢出时回滚"对象引用"类 export，
		// 避免重复登记（分片路径会重新触发引用 export）。
		Private::FNetExportRollbackScope ExportRollBack(SerializationContext);

		if (bSerializeWithObject)
		{
			Blob->SerializeWithObject(SerializationContext, Blob->GetNetObjectReference().GetRefHandle());
		}
		else
		{
			Blob->Serialize(SerializationContext);
		}

		// Errors are bad. We cannot recover from this.
		// 中文：序列化逻辑错误（非溢出）—— 数据不可恢复，直接失败。
		if (SerializationContext.HasError())
		{
			return false;
		}
	}

	Writer.CommitWrites();

	// Blob needs splitting!
	if (Writer.IsOverflown())
	{
		// This will redo all the serialization work, but it should rarely happen.
		// 中文：溢出 → 转父类完整分片路径（会重做序列化）。少见且代价可接受。
		if (bSerializeWithObject)
		{
			return Super::SplitNetBlob(SerializationContext, Blob->GetNetObjectReference(), reinterpret_cast<const TRefCountPtr<FNetBlob>&>(Blob), OutPartialBlobs);
		}
		else
		{
			return Super::SplitNetBlob(SerializationContext, reinterpret_cast<const TRefCountPtr<FNetBlob>&>(Blob), OutPartialBlobs);
		}
	}
	else
	{
		// 中文：未溢出 → 直接 ShrinkWrap 单条发出。Payload 缓冲移交，避免拷贝。
		FShrinkWrapNetObjectAttachment* ShrinkWrapNetBlob = new FShrinkWrapNetObjectAttachment(SerializationContext, Blob, MoveTemp(Payload), Writer.GetPosBits());
		OutPartialBlobs.AddDefaulted_GetRef() = ShrinkWrapNetBlob;
		return true;
	}
}

bool UPartialNetObjectAttachmentHandler::SplitRawDataNetBlob(const TRefCountPtr<UE::Net::FRawDataNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName) const
{
	using namespace UE::Net;

	const uint32 BitCountSplitThreshold =  GetConfig()->GetBitCountSplitThreshold() & ~31U;
	if (Blob->GetRawDataBitCount() > BitCountSplitThreshold)
	{
		// 中文：超阈值 → 按 PartialNetBlob 协议切片。RawData 不需要再走 Quantize，
		// FPartialNetBlob 内部对 RawData 类型走 fast path（直接位流拷贝）。
		const UPartialNetObjectAttachmentHandlerConfig* ThisConfig = GetConfig();

		FNetBlobCreationInfo CreationInfo = {};
		CreationInfo.Type = GetNetBlobType();
		// 中文：RawData 分片始终标记 Reliable —— 巨型对象数据流要求每片必达。
		CreationInfo.Flags = ENetBlobFlags::Reliable;

		FPartialNetBlob::FSplitParams SplitParams = {};
		SplitParams.MaxPartBitCount = ThisConfig->GetMaxPartBitCount();
		SplitParams.MaxPartCount = ThisConfig->GetMaxPartCount();
		SplitParams.bSerializeWithObject = false;
		if (InDebugName != nullptr)
		{
			SplitParams.DebugName = *InDebugName;
		}

		return FPartialNetBlob::SplitNetBlob(CreationInfo, SplitParams, Blob, OutPartialBlobs);
	}
	// No splitting required.
	else
	{
		// 中文：未达阈值 → 原样直通，不做任何包装。
		OutPartialBlobs.AddDefaulted_GetRef() = Blob.GetReference();
		return true;
	}
}
