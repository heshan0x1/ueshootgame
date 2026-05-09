// Copyright Epic Games, Inc. All Rights Reserved.

// =================================================================================================
// PartialNetBlob.cpp —— FPartialNetBlob 的序列化与切分实现
// -------------------------------------------------------------------------------------------------
// 本文件实现"超 MTU 大 blob 的分片协议"。原 blob（FNetBlob 或 FRawDataNetBlob）
// 经 SplitNetBlob 切成若干 FPartialNetBlob，每片：
//   * 共享一段全局递增的 SequenceNumber（用于接收端按序重组、检测乱序/重复）；
//   * 仅首片携带 OriginalCreationInfo（typeId/flags 等）+ PartCount，其余片靠 SequenceNumber 推断；
//   * Payload 用 32-bit 对齐方便 memcpy；序列化时仅写实际位数（PayloadBitCount，uint16），
//     节省最后一片的空载 bit。
//
// 核心字段：
//   - SequenceNumber（uint32）：所有片在同一 ReplicationSystem 实例内单调递增；
//   - PartCount（uint16）：仅首片填充，告知重组器总片数；
//   - PayloadBitCount（uint16）：本片实际位数；
//   - SequenceFlags：当前仅 IsFirstPart 一位；
//   - OriginalCreationInfo：原 blob 的 CreationInfo（typeId + flags）；
//   - OriginalBlob：仅首片为承载 export（HasExports 时）保留指针；
//   - ObjectReferenceExportsArray / NetTokenExportsArray：首片携带的待导出对象引用 / NetToken。
//
// 接收端重组由 FNetBlobAssembler 完成（见 NetBlobAssembler.h/.cpp），其策略：
//   * 按 SequenceNumber 顺序累加；
//   * 见到 IsFirstPart 即重置 + 记录 PartCount；
//   * 收齐 PartCount 片后用 OriginalCreationInfo 还原原 blob 类型，
//     调用 INetBlobReceiver::CreateNetBlob 创建空壳，最后 Deserialize 内部位流。
// =================================================================================================

#include "Iris/ReplicationSystem/NetBlob/PartialNetBlob.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net::Private
{
	/*
	 * We need a unique sequence number per split blob to detect out of order sequences. In theory this requirement is only needed for split blobs sent via the same queue or whatever mechanism is used.
	 * As the overhead of large sequence numbers should be relatively low compared to the split payload size and sharing a global sequence number avoids bloating the splitting API we have this one shared atomic.
	 * Each split NetBlob will reserve unique sequence numbers for all of its parts. If there are multiple ReplicationSystems sharing the atomic adds relatively little growing of the sequence numbers.
	 * Splitting blobs is a special case mainly used to enable replicating very large objects.
	 */
	// 全局序号生成器：所有 PartialNetBlob（跨多 ReplicationSystem 实例）共享递增。
	// - 每次 SplitNetBlob 调用会预占 PartialBlobCount 个连续序号；
	// - 不使用 std::atomic 是因为 Iris 的 SplitNetBlob 调用都在主线程串行；
	// - 用 uint32 而非 uint16：32-bit 序号空间足够大（即使每帧切 1000 片也能跑数十天才回绕），
	//   远高于"接收端重组窗口"（典型几百片）的需求，故无须担心重排导致的歧义。
	// - 多 RS 共享同一序号生成器只是会让序号增长稍快一些，不会产生冲突（每片仍然 globally unique）。
	static uint32 PartialNetBlobGlobalSequenceNumber;
}

namespace UE::Net
{

/** 构造函数：仅初始化基类与 OriginalCreationInfo（首片才会被填充实际 typeId）。*/
FPartialNetBlob::FPartialNetBlob(const FNetBlobCreationInfo& CreationInfo)
: FNetBlob(CreationInfo)
, OriginalCreationInfo({})
{
}

/** 取首片携带的 NetObjectReference 导出列表（仅首片可能非空）。 */
TArrayView<const FNetObjectReference> FPartialNetBlob::GetNetObjectReferenceExports() const
{
	return MakeConstArrayView<>(ObjectReferenceExportsArray);
}

/** 取首片携带的 NetToken 导出列表（仅首片可能非空）。 */
TArrayView<const FNetToken> FPartialNetBlob::GetNetTokenExports() const
{
	return MakeConstArrayView<>(NetTokenExportsArray);
}

/**
 * SerializeWithObject / DeserializeWithObject：附件型分片 blob 的入口。
 * 此处 RefHandle 仅用于上层调度（决定 batch 隶属哪个对象）；具体片内容与对象无关，
 * 因此实现统一转发到 InternalSerialize/InternalDeserialize。
 */
void FPartialNetBlob::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	InternalSerialize(Context);
}

void FPartialNetBlob::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	InternalDeserialize(Context);
}

/** 普通（非附件型）分片 blob 的入口；与 *WithObject 实现一致。 */
void FPartialNetBlob::Serialize(FNetSerializationContext& Context) const
{
	InternalSerialize(Context);
}

void FPartialNetBlob::Deserialize(FNetSerializationContext& Context)
{
	InternalDeserialize(Context);
}

/**
 * 单片序列化主流程（位流布局）：
 *   [PackedUint32 SequenceNumber]
 *   [bool IsFirstPart]
 *     IsFirstPart==true 时还会写：
 *       [PackedUint16 PartCount-1]
 *       [SerializeCreationInfo OriginalCreationInfo]   // 原 blob 的 typeId/flags
 *   [PackedUint16 PayloadBitCount]
 *   [Payload (PayloadBitCount bits)]
 *
 * 设计要点：
 * - 仅首片携带 OriginalCreationInfo + PartCount，节省后续片的开销；
 * - PayloadBitCount 用 uint16 → 单片最大约 8 KB，与 SplitNetBlob 的 MaxPartBitCount<65536U 自洽；
 * - 写完后通过 NetTrace 给序号/总片数加上 trace scope，便于 trace 工具按片回放。
 */
void FPartialNetBlob::InternalSerialize(FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// Use user provided debug name as outer scope. Terminate scope immediately if none was provided.
	// 若调用方提供了 DebugName，使用它作为外层 trace scope；否则立即关闭以避免空 scope 占位。
	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(UserProvidedScope, &DebugName, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	if (DebugName.Name == nullptr)
	{
		UE_NET_TRACE_EXIT_NAMED_SCOPE(UserProvidedScope);
	}
	UE_NET_TRACE_SCOPE(PartialNetBlob, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(SequenceScope, static_cast<const TCHAR*>(nullptr), *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
#if UE_NET_TRACE_ENABLED
	// VeryVerbose 级别开启时，把序号/首片信息写入 SequenceScope 的名字便于 trace 阅读。
	if (FNetTrace::GetNetTraceVerbosityEnabled(ENetTraceVerbosity::VeryVerbose))
	{
		TStringBuilder<64> Builder;
		if (IsFirstPart())
		{
			Builder.Appendf(TEXT("Seq %u First part of %u"), SequenceNumber, PartCount);
		}
		else
		{
			Builder.Appendf(TEXT("Seq %u"), SequenceNumber);
		}
		UE_NET_TRACE_SET_SCOPE_NAME(SequenceScope, Builder.ToString());
	}
#endif // UE_NET_TRACE_ENABLED

	// 写入 SequenceNumber（变长 1-5 字节，多数情况下 1-2 字节）。
	WritePackedUint32(Writer, SequenceNumber);
	if (Writer->WriteBool(IsFirstPart()))
	{
		// 首片：补写 PartCount-1（用 -1 编码节省一位常见值"1 片"）。
		WritePackedUint16(Writer, PartCount - 1U);

		UE_NET_TRACE_SCOPE(OriginalCreationInfo, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		// 写原 blob 的 CreationInfo（typeId + flags），接收端重组时据此创建原 blob 实例。
		SerializeCreationInfo(Context, OriginalCreationInfo);
	}

	// 写本片的 Payload（PayloadBitCount + 实际位数据）。
	InternalSerializeBlob(Context);
}

/**
 * 单片反序列化主流程（与 InternalSerialize 严格对偶）：
 * - 先读 SequenceNumber；
 * - 再读 1 bit 判断是否首片；首片则继续读 PartCount + OriginalCreationInfo；
 * - 最后读 Payload。
 *
 * 序号通过 SequenceFlags 中的 IsFirstPart 位标识，以便接收端 Assembler 决策"是否开新一组重组缓冲"。
 */
void FPartialNetBlob::InternalDeserialize(FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	UE_NET_TRACE_SCOPE(PartialNetBlob, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(SequenceScope, static_cast<const TCHAR*>(nullptr), *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

	SequenceNumber = ReadPackedUint32(Reader);
	SequenceFlags = ESequenceFlags::None;
	if (Reader->ReadBool())
	{
		// 首片：填充 SequenceFlags 与 PartCount，并解出原 blob 的 CreationInfo。
		SequenceFlags = ESequenceFlags::IsFirstPart;
		PartCount = ReadPackedUint16(Reader) + 1U;

		UE_NET_TRACE_SCOPE(OriginalCreationInfo, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		DeserializeCreationInfo(Context, OriginalCreationInfo);
	}

	// 读取 Payload。
	InternalDeserializeBlob(Context);

#if UE_NET_TRACE_ENABLED
	// 反序列化完成后再给 trace scope 加上人类可读的标题（与序列化端对称）。
	if (FNetTrace::GetNetTraceVerbosityEnabled(ENetTraceVerbosity::VeryVerbose))
	{
		TStringBuilder<64> Builder;
		if (IsFirstPart())
		{
			Builder.Appendf(TEXT("Seq %u First part of %u"), SequenceNumber, PartCount);
		}
		else
		{
			Builder.Appendf(TEXT("Seq %u"), SequenceNumber);
		}
		UE_NET_TRACE_SET_SCOPE_NAME(SequenceScope, Builder.ToString());
	}
#endif // UE_NET_TRACE_ENABLED
}

/** 写本片 Payload：长度（uint16 位数）+ 紧随的位流。 */
void FPartialNetBlob::InternalSerializeBlob(FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	UE_NET_TRACE_SCOPE(Payload, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	WritePackedUint16(Writer, PayloadBitCount);
	Writer->WriteBitStream(Payload.GetData(), 0U, PayloadBitCount);
}

/** 读本片 Payload：先读位数后读位数据；按 32-bit 对齐分配 Payload 数组以便 memcpy。 */
void FPartialNetBlob::InternalDeserializeBlob(FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	UE_NET_TRACE_SCOPE(Payload, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	PayloadBitCount = ReadPackedUint16(Reader);
	// 上取整到 word：FBitArrayView 的最小存储单位是 32-bit。
	Payload.SetNumUninitialized((PayloadBitCount + 31U)/32U);
	Reader->ReadBitStream(Payload.GetData(), PayloadBitCount);
}

/**
 * 把任意 FNetBlob 切分为若干 FPartialNetBlob。
 *
 * 算法：
 * 1) **试序列化**：将原 blob 序列化到临时位流缓冲区。由于 FNetBlob 的实际位长无法预知，
 *    采用"先试 128 KB，溢出则倍增"的策略，最大不超过 MaxPartBitCount * MaxPartCount。
 *    每次重试前需 RollBack ExportContext（FNetExportRollbackScope），以免上次失败留下脏导出。
 * 2) **切片**：把序列化后的位流按 MaxPartBitCount（已对齐到 32-bit）切成若干 FPartialNetBlob。
 *    详细工作交给 SplitPayload 完成。
 *
 * @param Context           外层序列化上下文，用于派生 SubContext + 取 ExportContext。
 * @param CreationInfo      新建 FPartialNetBlob 的 CreationInfo（typeId 通常为 PartialNetBlobHandler）。
 * @param SplitParams       切分参数：MaxPartBitCount/MaxPartCount/DebugName/bSerializeWithObject 等。
 * @param Blob              待切分的原 blob。
 * @param OutPartialBlobs   输出：所有片的 RefCount 引用列表（按序）。
 * @return 成功返回 true；序列化错误或缓冲超限返回 false（OutPartialBlobs 不变）。
 *
 * 边界处理：
 * - MaxPartBitCount 必须 > 31 且 < 65536（受 PayloadBitCount 字段宽度限制）；
 * - MaxPartCount 必须 > 0 且 < 65536（受 PartCount 字段宽度限制）；
 * - 缓冲区扩容上限为 MaxTotalBitCount（=MaxPartBitCount × MaxPartCount，对齐到 32-bit）；
 *   超此限制视为切分失败。
 */
bool FPartialNetBlob::SplitNetBlob(FNetSerializationContext& Context, const FNetBlobCreationInfo& CreationInfo, const FPartialNetBlob::FSplitParams& SplitParams, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs)
{
	// 参数合法性：MaxPartBitCount/MaxPartCount 都必须落在 16-bit 字段能表示的范围内。
	check(SplitParams.MaxPartBitCount > 31U && SplitParams.MaxPartBitCount < 65536U && SplitParams.MaxPartCount > 0 && SplitParams.MaxPartCount < 65536U);
	if (!Blob.IsValid())
	{
		return false;
	}

	// We have no idea what the internals of the FNetBlob look like. We must serialize it to a temporary buffer.
	// 由于 FNetBlob 的内部结构未知，必须先序列化到临时缓冲区才能确定实际位长。

	// 切分总位数上限（对齐到 word 便于 memcpy）。
	const uint32 MaxTotalBitCount = (SplitParams.MaxPartBitCount*SplitParams.MaxPartCount) & ~31U;

	// Want the part bit count to be a multiple of 32 so we can safely memcpy.
	// 单片位数对齐到 32-bit，确保切分时按 word 拷贝不越界。
	const uint32 MaxPartBitCount = SplitParams.MaxPartBitCount & ~31U;

	// Allocate a temporary buffer that is 128KB to begin with.
	// 临时缓冲区起步 128 KB；多数 blob 远小于此，避免一次性大内存预分配。
	TArray<uint32> Payload;
	constexpr uint32 InitialPayloadBitCountAttempt = 128U*1024U*8U;
	uint32 CurrentPayloadBitCount = FPlatformMath::Min(MaxTotalBitCount, InitialPayloadBitCountAttempt);

	// Trial and error. We don't want to allocate a ton of memory to begin with as we're not likely to need much.
	// 试错循环：序列化失败（位流溢出）→ 缓冲翻倍重试，直到成功或达到上限。
	bool bSuccess = false;
	do
	{
		Payload.SetNumUninitialized(CurrentPayloadBitCount/32U);

		FNetBitStreamWriter Writer;
		Writer.InitBytes(Payload.GetData(), static_cast<uint32>(Payload.Num())*4U);

		// 派生 SubContext 共享 ExportContext / ErrorContext，但写流指向临时 Writer。
		FNetSerializationContext SubContext = Context.MakeSubContext(&Writer);
		// 错误/溢出时回滚 export，避免重试时产生重复导出登记。
		Private::FNetExportRollbackScope ExportRollBack(SubContext);

		if (SplitParams.bSerializeWithObject)
		{
			Blob->SerializeWithObject(SubContext, SplitParams.NetObjectReference.GetRefHandle());
		}
		else
		{
			Blob->Serialize(SubContext);
		}

		// If there was an actual error we are unlikely to succeed.
		// 真正的语义错误（非空间不足）→ 直接放弃，重试也是徒劳。
		if (SubContext.HasError())
		{
			return false;
		}

		// If get a bitstream overflow then grow the buffer and retry.
		// 仅是缓冲不够 → 倍增空间继续尝试。
		bSuccess = !Writer.IsOverflown();
		if (!bSuccess)
		{
			// Again, we don't know how much buffer space we need. Double it.
			const uint32 NewPayloadBitCount = FPlatformMath::Min(2U*CurrentPayloadBitCount, MaxTotalBitCount);
			if (NewPayloadBitCount <= CurrentPayloadBitCount)
			{
				// Buffer space exhausted. We cannot split the blob.
				// 上限已达 → blob 太大无法切分，失败。
				return false;
			}

			CurrentPayloadBitCount = NewPayloadBitCount;
			continue;
		}

		// Adjust the payload bit count to the final value
		// 提交写入并取实际位长（用于切片）。
		Writer.CommitWrites();
		CurrentPayloadBitCount = Writer.GetPosBits();
	} while (!bSuccess);

	// At this point we've successfully serialized the blob to our buffer. Time to split.
	// 序列化已成功，下面把缓冲按 MaxPartBitCount 切成 N 片。
	{
		FPayloadSplitParams PayloadSplitParams;
		PayloadSplitParams.DebugName = SplitParams.DebugName;
		PayloadSplitParams.CreationInfo = CreationInfo;
		PayloadSplitParams.OriginalBlob = Blob.GetReference();
		PayloadSplitParams.OriginalCreationInfo = Blob->GetCreationInfo();
		PayloadSplitParams.Payload = Payload.GetData();
		PayloadSplitParams.PayloadBitCount = CurrentPayloadBitCount;
		PayloadSplitParams.PartBitCount = MaxPartBitCount;
		// 把当前 ExportContext 中"本批次未导出"的对象引用与 NetToken 转给首片携带，
		// 这样接收端在拼回原 blob 时同样能解析这些引用。
		if (Private::FNetExportContext* ExportContext = Context.GetExportContext())
		{			
			PayloadSplitParams.ObjectReferencesPendingExport = MakeConstArrayView<>(ExportContext->GetBatchExports().ReferencesPendingExportInCurrentBatch);
			PayloadSplitParams.NetTokensPendingExport = MakeConstArrayView<>(ExportContext->GetBatchExports().NetTokensPendingExportInCurrentBatch);
		}

		SplitPayload(PayloadSplitParams, OutPartialBlobs);
	}

	return true;
}

/**
 * 重载：直接切分 FRawDataNetBlob。
 *
 * 与上面"先序列化再切分"不同：FRawDataNetBlob 已经持有原始位流，不需要重新序列化，
 * 直接按 MaxPartBitCount 切片即可。
 *
 * @return 始终返回 true（除非 Blob 无效）。
 */
bool FPartialNetBlob::SplitNetBlob(const FNetBlobCreationInfo& CreationInfo, const FSplitParams& SplitParams, const TRefCountPtr<FRawDataNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs)
{
	check(SplitParams.MaxPartBitCount > 31U && SplitParams.MaxPartBitCount < 65536U && SplitParams.MaxPartCount > 0 && SplitParams.MaxPartCount < 65536U);
	if (!Blob.IsValid())
	{
		return false;
	}

	FPayloadSplitParams PayloadSplitParams = {};
	PayloadSplitParams.DebugName = SplitParams.DebugName;
	PayloadSplitParams.CreationInfo = CreationInfo;
	PayloadSplitParams.OriginalBlob = Blob.GetReference();
	PayloadSplitParams.OriginalCreationInfo = Blob->GetCreationInfo();
	PayloadSplitParams.Payload = Blob->GetRawData().GetData();
	PayloadSplitParams.PayloadBitCount = Blob->GetRawDataBitCount();
	PayloadSplitParams.PartBitCount = SplitParams.MaxPartBitCount & ~31U;

	SplitPayload(PayloadSplitParams, OutPartialBlobs);
	return true;
}

/**
 * 把已序列化好的 Payload 按 PartBitCount 切片，构造 FPartialNetBlob 列表。
 *
 * 关键点：
 * 1) 计算总片数 PartialBlobCount = ceil(PayloadBitCount / PartBitCount)；
 * 2) 一次性预占连续的 SequenceNumber（让接收端重组依赖严格递增的序号）；
 * 3) 仅首片携带 OriginalCreationInfo + Exports（NetObjectRef / NetToken / 透传原 blob 的 Exports）；
 * 4) 各片 Payload 用 32-bit 对齐 memcpy；最后一片可能不满，PayloadBitCount 字段记录实际位数。
 */
void FPartialNetBlob::SplitPayload(const FPartialNetBlob::FPayloadSplitParams& SplitParams, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs)
{
	// 总片数 = ceil(PayloadBitCount / PartBitCount)。
	const uint32 PartialBlobCount = (SplitParams.PayloadBitCount + SplitParams.PartBitCount - 1U)/SplitParams.PartBitCount;
	OutPartialBlobs.Reserve(OutPartialBlobs.Num() + int32(PartialBlobCount));
	
	// Reserve sequence numbers for all parts. 
	// 一次性预占连续序号，保证同一组所有片在全局序列空间中是严格相邻的。
	uint32 SequenceNumber = Private::PartialNetBlobGlobalSequenceNumber;
	Private::PartialNetBlobGlobalSequenceNumber += PartialBlobCount;

	uint32 PayloadBitOffset = 0U;

	// Do we have exports	
	// 主循环：构造每一片 FPartialNetBlob，逐次拷贝 Payload 切片。
	for (uint32 PartIt = 0, PartEndIt = PartialBlobCount; PartIt != PartEndIt; ++PartIt)
	{
		const bool bIsFirstPart = PartIt == 0U;

		FPartialNetBlob* PartialBlob = new FPartialNetBlob(SplitParams.CreationInfo);
		PartialBlob->SetDebugName(SplitParams.DebugName);
		PartialBlob->OriginalCreationInfo = SplitParams.OriginalCreationInfo;
		PartialBlob->PartCount = static_cast<uint16>(PartialBlobCount);
		PartialBlob->SequenceFlags = (bIsFirstPart ? ESequenceFlags::IsFirstPart : ESequenceFlags::None);
		PartialBlob->SequenceNumber = SequenceNumber++;
		// 仅首片需要承载 exports：原 blob 自身有导出 OR 入参显式传入了 pending exports。
		if (bIsFirstPart && (EnumHasAnyFlags(SplitParams.OriginalCreationInfo.Flags, ENetBlobFlags::HasExports) || !SplitParams.ObjectReferencesPendingExport.IsEmpty() || !SplitParams.NetTokensPendingExport.IsEmpty()))
		{
			PartialBlob->OriginalBlob = SplitParams.OriginalBlob;
			PartialBlob->CreationInfo.Flags |= ENetBlobFlags::HasExports;
			PartialBlob->ObjectReferenceExportsArray.Append(SplitParams.ObjectReferencesPendingExport);
			PartialBlob->NetTokenExportsArray.Append(SplitParams.NetTokensPendingExport);
			// 透传原 blob 自身记录的 NetObjectReferenceExports（去重 AddUnique）。
			if (EnumHasAnyFlags(SplitParams.OriginalCreationInfo.Flags, ENetBlobFlags::HasExports) && SplitParams.OriginalBlob)
			{
				for (TConstArrayView<FNetObjectReference> ObjectReferences = SplitParams.OriginalBlob->CallGetNetObjectReferenceExports(); const FNetObjectReference& ObjectReference : ObjectReferences)
				{
					PartialBlob->ObjectReferenceExportsArray.AddUnique(ObjectReference);
				}
			}
		}

		// Copy relevant data from our temporary buffer.
		// 计算本片有效位数：未必每片都满（最后一片通常不满）。
		const uint32 PartialBlobBitCount = FPlatformMath::Min(SplitParams.PayloadBitCount - PayloadBitOffset, SplitParams.PartBitCount);
		const uint32 PartialBlobWordCount = (PartialBlobBitCount + 31U)/32U;
		const uint32 PayloadWordOffset = PayloadBitOffset/32U;
		PartialBlob->PayloadBitCount = static_cast<uint16>(PartialBlobBitCount);
		PartialBlob->Payload.SetNumUninitialized(PartialBlobWordCount);
		// 32-bit 对齐 memcpy：因为 PartBitCount 已对齐到 32-bit，且临时缓冲也是 word 数组。
		FPlatformMemory::Memcpy(PartialBlob->Payload.GetData(), SplitParams.Payload + PayloadWordOffset, PartialBlobWordCount*4U);

		PayloadBitOffset += SplitParams.PartBitCount;

		OutPartialBlobs.Add(PartialBlob);
	}
}

}
