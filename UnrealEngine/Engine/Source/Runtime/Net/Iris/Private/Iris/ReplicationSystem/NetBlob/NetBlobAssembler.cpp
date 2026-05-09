// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobAssembler.cpp —— 分片 blob 重组实现。
// 状态机要点：
//   1) 第一片（IsFirstPart）必须把 PartCount/PayloadBitCount 都校验通过后初始化 Payload buffer。
//   2) 后续片要求 (RefHandle, SequenceNumber) 严格连续，并且 PayloadBitCount 不超过第一片
//      （末片可以更小）。
//   3) 可靠序列要求严格走完整轮，途中不允许重启；非可靠序列允许在任意位置接收新的"第一片"。
//   4) 所有错误都先写入 Context.SetError 并设置 bIsBrokenSequence，使后续 Add 调用直接失败。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetBlobAssembler.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"
#include "Iris/ReplicationSystem/NetBlob/RawDataNetBlob.h"
#include "Iris/ReplicationSystem/NetBlob/SequentialPartialNetBlobHandler.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetSerializationContext.h"

namespace UE::Net
{

// 分片错位错误名：序号跳变 / 可靠分片中断 / RefHandle 不一致都会用此错误上报。
static const FName NetError_PartialNetBlobSequenceError("Out of order PartialNetBlob.");

FNetBlobAssembler::FNetBlobAssembler()
{
}

// 必须传入 PartialNetBlobHandlerConfig，否则后续 AddPartialNetBlob 全部拒绝。
void FNetBlobAssembler::Init(const FNetBlobAssemblerInitParams& InitParams)
{
	PartialNetBlobHandlerConfig = InitParams.PartialNetBlobHandlerConfig;
	ensureMsgf(PartialNetBlobHandlerConfig != nullptr, TEXT("NetBlobAssembler requires a PartialNetBlobHandlerConfig"));
}

void FNetBlobAssembler::AddPartialNetBlob(FNetSerializationContext& Context, FNetRefHandle InRefHandle, const TRefCountPtr<FPartialNetBlob>& PartialNetBlob)
{
	// 默认未就绪；只有当本次 Add 后所有片都到齐才会重新置 true。
	bIsReadyToAssemble = false;

	if (!ensureMsgf(PartialNetBlobHandlerConfig != nullptr, TEXT("NetBlobAssembler requires a PartialNetBlobHandlerConfig")))
	{
		Context.SetError(GNetError_InvalidValue);
		return;
	}

	// We expect broken sequences to be handled as soon as they're reported. We've already reported the error once.
	// 一旦序列损坏即视作永久失败：调用方应该立即处理并复位 Assembler，否则继续追加只会
	// 反复抛出同一个错误。
	if (bIsBrokenSequence)
	{
		Context.SetError(NetError_PartialNetBlobSequenceError);
		return;
	}

	const uint32 SequenceNumber = PartialNetBlob->GetSequenceNumber();
	const bool bIsFirstPart = PartialNetBlob->IsFirstPart();

	// Broken sequence detection. For reliable blobs the NextPartIndex and NextSequenceSumber must match. If reliability changes we need some additional validation.
	// 错位检测：序号必须等于期望值；可靠 ↔ 不可靠切换中也只允许在 IsFirstPart 时发生。
	const bool bIsReliable = PartialNetBlob->IsReliable();
	if (SequenceNumber != NextSequenceSumber || bIsReliable != bIsProcessingReliable)
	{
		// Reliable sequences are expected to be fully processed before proceeding to a new sequence. If we're in the middle of processing everything needs to match.
		// 正在处理可靠序列时，任何错位都算永久失败。
		if (bIsProcessingReliable)
		{
			bIsBrokenSequence = true;
			Context.SetError(NetError_PartialNetBlobSequenceError);
			return;
		}

		// Gracefully handle going from unreliable to first part of blob, regardless of its reliability.
		// 不可靠序列允许丢包后从新一轮"第一片"重启；如果不是第一片但序号又不对，则视情况报错：
		//   - 新序列若是 Reliable：必须报错（可靠协议不允许丢失）。
		//   - 否则：静默丢弃当前 Assembler 状态。
		if (!bIsFirstPart && SequenceNumber != NextSequenceSumber)
		{
			bIsBrokenSequence = true;
			if (bIsReliable)
			{
				Context.SetError(NetError_PartialNetBlobSequenceError);
			}
			return;
		}
	}

	bIsProcessingReliable = bIsReliable;

	if (bIsFirstPart)
	{
		// 第一片：用本片的 PartCount 与 PayloadBitCount 做边界校验，初始化重组 buffer。
		const uint32 PartCount = PartialNetBlob->GetPartCount();
		NextSequenceSumber = SequenceNumber;
		LastPartSequenceNumber = SequenceNumber + PartialNetBlob->GetPartCount() - 1U;

		if (PartCount == 0)
		{
			bIsBrokenSequence = true;
			Context.SetError(GNetError_InvalidValue);
			return;
		}

		// 防御：PartCount 不得超过 ini 配置上限（防止恶意客户端申请超大缓冲）。
		if (PartCount > PartialNetBlobHandlerConfig->GetMaxPartCount())
		{
			bIsBrokenSequence = true;
			Context.SetError(GNetError_InvalidValue);
			return;
		}

		const uint32 PayloadBitCount = PartialNetBlob->GetPayloadBitCount();
		// We allow part sizes to be lower than the config value in case it was hotfixed.
		// 单片大小允许小于 config 上限（hotfix 后变小可兼容），但不允许更大。
		if (PayloadBitCount > PartialNetBlobHandlerConfig->GetMaxPartBitCount())
		{
			bIsBrokenSequence = true;
			Context.SetError(GNetError_InvalidValue);
			return;
		}

		// 记录承载该 blob 的对象引用；后续片必须保持一致。
		RefHandle = InRefHandle;

		// 记录原始 CreationInfo（包含 typeId + Reliable）；Assemble 时用它创建空 blob。
		NetBlobCreationInfo = PartialNetBlob->GetOriginalCreationInfo();

		// The first part must be greater or equal to any subsequent parts or we will report an error.
		// 第一片大小作为后续每片的上限基准（最后一片例外，可以更小）。
		FirstPayloadBitCount = PayloadBitCount;

		// Prepare a bitstream that can hold the full blob.
		// 按 (单片字节数 × PartCount) 申请连续 buffer，BitWriter 定位到首位。
		const uint32 MaxByteCountPerPart = (PayloadBitCount + 7U)/8U;
		Payload.SetNumUninitialized((MaxByteCountPerPart*PartCount + 3U)/4U);
		BitWriter.InitBytes(Payload.GetData(), Payload.Num()*4U);

		// Store partial payload.
		// 把第一片的 payload bits 写入 buffer 起始位置。
		BitWriter.WriteBitStream(PartialNetBlob->GetPayload(), 0, PayloadBitCount);
	}
	else
	{
		// 后续片：必须与首片 RefHandle 相同。
		if (InRefHandle != RefHandle)
		{
			bIsBrokenSequence = true;
			Context.SetError(NetError_PartialNetBlobSequenceError);
			return;
		}

		const uint32 PayloadBitCount = PartialNetBlob->GetPayloadBitCount();
		const uint32 PayloadByteCount = (PayloadBitCount + 7U)/8U;
		// All parts except the last one is expected to match the first part. The last part may be smaller.
		// 大小校验：除最后一片可以更小外，其它片 PayloadBitCount 必须等于首片；
		// 任何一片都不允许超过首片大小。
		if (((PayloadBitCount != FirstPayloadBitCount) && (NextSequenceSumber != LastPartSequenceNumber)) || (PayloadBitCount > FirstPayloadBitCount))
		{
			bIsBrokenSequence = true;
			Context.SetError(GNetError_InvalidValue);
			return;
		}

		// Store partial payload.
		// 追加写入：BitWriter 内部记录当前位偏移，无需额外计算。
		BitWriter.WriteBitStream(PartialNetBlob->GetPayload(), 0, PayloadBitCount);
	}

	// 走到末片即就绪；否则期望序号 +1 等待下一片。
	if (NextSequenceSumber == LastPartSequenceNumber)
	{
		bIsReadyToAssemble = true;
	}
	else
	{
		++NextSequenceSumber;
	}
}

TRefCountPtr<FNetBlob> FNetBlobAssembler::Assemble(FNetSerializationContext& Context)
{
	// Assemble 后即重置 ready 标志，等待下一轮 first part。
	bIsReadyToAssemble = false;

	// 通过 Context 拿到当前线程的 INetBlobReceiver（实际是 FNetBlobHandlerManager），
	// 让它按 typeId 反查对应 handler 创建一个空 blob 实例。
	INetBlobReceiver* BlobHandler = Context.GetNetBlobReceiver();
	checkSlow(BlobHandler != nullptr);

	const TRefCountPtr<FNetBlob>& NetBlob = BlobHandler->CreateNetBlob(NetBlobCreationInfo);
	if (Context.HasError() || !NetBlob.IsValid())
	{
		// typeId 无效或 handler 未注册：报告 UnsupportedNetBlob。
		Context.SetError(GNetError_UnsupportedNetBlob);
		return nullptr;
	}

	// 提交 BitWriter 内部缓存的位写入，使后续读取看到完整位流长度。
	BitWriter.CommitWrites();

	// Fast path for RawDataNetBlobs
	// 原始字节快速路径：blob 是 FRawDataNetBlob 派生 → 直接搬移 payload 数组，
	// 不再触发 blob->Deserialize（避免一次 dequantize 后再 quantize 的多余转换）。
	if (EnumHasAnyFlags(NetBlob->GetCreationInfo().Flags, ENetBlobFlags::RawDataNetBlob))
	{
		FRawDataNetBlob* RawDataNetBlob = static_cast<FRawDataNetBlob*>(NetBlob.GetReference());
		// The BitWriter is currently pointing to the payload data so we want to make sure we're not leaving
		// it in an undefined state after moving the payload.
		// BitWriter 仍指向 Payload 内存，故在 MoveTemp 之前先记录写入的总位数并清空 BitWriter。
		const uint32 PayloadBitCount = BitWriter.GetPosBits();
		BitWriter = FNetBitStreamWriter();
		RawDataNetBlob->SetRawData(MoveTemp(Payload), PayloadBitCount);
	}
	else
	{
		// 标准路径：在重组缓冲上构造一个 BitReader，让 blob 自己反序列化。
		FNetBitStreamReader BitReader;
		BitReader.InitBits(Payload.GetData(), BitWriter.GetPosBits());

		// 临时子上下文，使 blob 内部错误不会污染外层 trace collector。
		FNetSerializationContext ReadContext = Context.MakeSubContext(&BitReader);
		ReadContext.SetTraceCollector(nullptr);

		if (RefHandle.IsValid())
		{
			NetBlob->DeserializeWithObject(ReadContext, RefHandle);
		}
		else
		{
			NetBlob->Deserialize(ReadContext);
		}

		if (ReadContext.HasErrorOrOverflow())
		{
			// Copy error
			// 把子上下文错误抬升回外层 Context，调用方据此判断是否丢弃该 blob。
			if (ReadContext.HasError())
			{
				Context.SetError(ReadContext.GetError());
			}
			else
			{
				Context.SetError(GNetError_BitStreamOverflow);
			}

			return nullptr;
		}

		// Bitstream mismatch?
		// 写入位数与读取位数必须严格相等，否则说明 blob 的 Serialize/Deserialize 不对称
		// → 视为 bitstream 损坏，丢弃。
		if (BitWriter.GetPosBits() != BitReader.GetPosBits())
		{
			Context.SetError(GNetError_BitStreamError);
			return nullptr;
		}
	}

	return NetBlob;
}

}
