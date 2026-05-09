// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ShrinkWrapNetBlob.cpp —— "塑封"已序列化数据 blob 实现
// -----------------------------------------------------------------------------
// 关键流程：
//   1) 上层在序列化原 blob 时已开启 ExportContext，序列化结束后 batch.NetTokensPendingExportInCurrentBatch
//      记录了"本次 blob 触发"的待导出 token；这些 token 的导出 ACK 是按"包"
//      跟踪的 —— 塑封实例必须把这份列表"复制一份"作为自己的 export 集，否则
//      多次发送时 token 会不被认为是 pending。
//   2) Serialize(WithObject) 写出位流；
//      - 启用 trace 时回退到原 blob.Serialize 路径，便于完整 trace 字段；
//      - 关闭 trace 时直接 InternalSerialize（位流拷贝），是常见快路径。
//   3) Attachment 子类 Serialize 时还要把对象引用加入 pending exports，因为
//      原 blob 是把 target/source object ref 当作普通字段写入位流的，框架本身
//      并不会再次解析这些字节去做 export 跟踪。
// 警告：
//   - DeserializeWithObject / Deserialize 直接 checkf(false) —— 接收端永远走
//     原 blob 类型，本类实例不应被还原。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/ShrinkWrapNetBlob.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net
{

// ---- FShrinkWrapNetBlob ----------------------------------------------------

// 构造：CreationInfo 沿用原 blob，把 Payload 移入；快照当前 batch 的 token export 列表，
// 若非空则在 Flags 上 OR 一个 HasExports 位（让发送层在写出该 blob 前先 export）。
FShrinkWrapNetBlob::FShrinkWrapNetBlob(FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& InOriginalBlob, TArray<uint32>&& Payload, uint32 PayloadBitCount)
: FNetBlob(InOriginalBlob->GetCreationInfo())
, OriginalBlob(InOriginalBlob)
, SerializedBlob(MoveTemp(Payload))
, SerializedBlobBitCount(PayloadBitCount)
{
	NetTokenExportsArray = Context.GetExportContext()->GetBatchExports().NetTokensPendingExportInCurrentBatch;
	CreationInfo.Flags |= NetTokenExportsArray.Num() ? ENetBlobFlags::HasExports : ENetBlobFlags::None;
}

// 中文：对象引用 export 转给原 blob —— 必须保留语义，因为某些 reference 只在
//       原 blob 的成员协议描述里能被收集到。
TArrayView<const FNetObjectReference> FShrinkWrapNetBlob::GetNetObjectReferenceExports() const
{
	return OriginalBlob->CallGetNetObjectReferenceExports();
}

// 中文：返回构造时快照的本批 NetToken pending export 列表。
TArrayView<const FNetToken> FShrinkWrapNetBlob::GetNetTokenExports() const
{
	return MakeArrayView<const FNetToken>(NetTokenExportsArray.GetData(), NetTokenExportsArray.Num());
}

// 中文：trace 模式下走原 blob 序列化以便 trace 中字段名清晰；否则直拷位流。
void FShrinkWrapNetBlob::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	if (Context.GetTraceCollector() != nullptr && OriginalBlob.IsValid())
	{
		OriginalBlob->SerializeWithObject(Context, RefHandle);
	}
	else
	{
		InternalSerialize(Context);
	}
}

// 中文：禁止 —— 接收端不会反序列化为本类型；调用即 fatal check。
void FShrinkWrapNetBlob::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	checkf(false, TEXT("%s"), TEXT("This function should not be called. Contact the networking team."));
}

void FShrinkWrapNetBlob::Serialize(FNetSerializationContext& Context) const
{
	if (Context.GetTraceCollector() != nullptr && OriginalBlob.IsValid())
	{
		OriginalBlob->Serialize(Context);
	}
	else
	{
		InternalSerialize(Context);
	}
}

void FShrinkWrapNetBlob::Deserialize(FNetSerializationContext& Context)
{
	checkf(false, TEXT("%s"), TEXT("This function should not be called. Contact the networking team."));
}

// 中文：核心快路径——位流拷贝。无需写位长度（接收端原 blob 自己知道字段长度）。
void FShrinkWrapNetBlob::InternalSerialize(FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	UE_NET_TRACE_SCOPE(ShrinkWrapNetBlob, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// No need to serialize the blob bit count as on the receiving side the original NetBlob will do all the deserialization.
	// 中文：不写位数 —— 接收端按原 blob 协议结构自然消费完即可。
	constexpr uint32 SerializedBlobBitOffset = 0U;
	Writer->WriteBitStream(SerializedBlob.GetData(), SerializedBlobBitOffset, SerializedBlobBitCount);
}


// ---- FShrinkWrapNetObjectAttachment ----------------------------------------

// 中文：构造与普通版相同；OriginalBlob 类型为 FNetObjectAttachment 派生。
FShrinkWrapNetObjectAttachment::FShrinkWrapNetObjectAttachment(FNetSerializationContext& Context, const TRefCountPtr<FNetObjectAttachment>& InOriginalBlob, TArray<uint32>&& Payload, uint32 PayloadBitCount)
: FNetBlob(InOriginalBlob->GetCreationInfo())
, OriginalBlob(InOriginalBlob)
, SerializedBlob(MoveTemp(Payload))
, SerializedBlobBitCount(PayloadBitCount)
{
	NetTokenExportsArray = Context.GetExportContext()->GetBatchExports().NetTokensPendingExportInCurrentBatch;
	CreationInfo.Flags |= NetTokenExportsArray.Num() ? ENetBlobFlags::HasExports : ENetBlobFlags::None;
}

TArrayView<const FNetObjectReference> FShrinkWrapNetObjectAttachment::GetNetObjectReferenceExports() const
{
	return OriginalBlob->CallGetNetObjectReferenceExports();
}

TArrayView<const FNetToken> FShrinkWrapNetObjectAttachment::GetNetTokenExports() const
{
	return MakeArrayView<const FNetToken>(NetTokenExportsArray.GetData(), NetTokenExportsArray.Num());
}

// 中文：SerializeWithObject —— 调用方已知道 RefHandle，原 blob 缓存的位流里没有
//       自己写 source object 引用（被 NetRefHandle 隐含）；但 target subobject ref
//       可能是 payload 的一部分，需要把 target 加入 pending exports。
void FShrinkWrapNetObjectAttachment::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	if (Context.GetTraceCollector() != nullptr && OriginalBlob.IsValid())
	{
		OriginalBlob->SerializeWithObject(Context, RefHandle);
	}
	else
	{
		// Add target exports
		// 中文：补登 target object 引用为 pending export，否则接收端解析 ref 时缺导出。
		if (Private::FNetExportContext* ExportContext = Context.GetExportContext())
		{
			const Private::FObjectReferenceCache* ObjectReferenceCache = Context.GetInternalContext()->ObjectReferenceCache;
			ObjectReferenceCache->AddPendingExport(*ExportContext, OriginalBlob->GetTargetObjectReference());
		}
		InternalSerialize(Context);
	}
}

void FShrinkWrapNetObjectAttachment::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	checkf(false, TEXT("%s"), TEXT("This function should not be called. Contact the networking team."));
}

// 中文：Serialize（不带 RefHandle）—— 上下文不知道 source 对象，所以 source 与
//       target 两个引用都要补登 export。
void FShrinkWrapNetObjectAttachment::Serialize(FNetSerializationContext& Context) const
{
	if (Context.GetTraceCollector() != nullptr && OriginalBlob.IsValid())
	{
		OriginalBlob->Serialize(Context);
	}
	else
	{
		// Add target exports
		// 中文：source（NetObjectReference）+ target（TargetObjectReference）都要登。
		if (Private::FNetExportContext* ExportContext = Context.GetExportContext())
		{
			const Private::FObjectReferenceCache* ObjectReferenceCache = Context.GetInternalContext()->ObjectReferenceCache;		
			ObjectReferenceCache->AddPendingExport(*ExportContext, OriginalBlob->GetNetObjectReference());
			ObjectReferenceCache->AddPendingExport(*ExportContext, OriginalBlob->GetTargetObjectReference());
		}
		InternalSerialize(Context);
	}
}

void FShrinkWrapNetObjectAttachment::Deserialize(FNetSerializationContext& Context)
{
	checkf(false, TEXT("%s"), TEXT("This function should not be called. Contact the networking team."));
}

void FShrinkWrapNetObjectAttachment::InternalSerialize(FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	UE_NET_TRACE_SCOPE(ShrinkWrapNetObjectAttachment, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// No need to serialize the blob bit count as on the receiving side the original NetBlob will do all the deserialization.
	// 中文：与普通版同 —— 不写位数，接收端按原协议消费。
	constexpr uint32 SerializedBlobBitOffset = 0U;
	Writer->WriteBitStream(SerializedBlob.GetData(), SerializedBlobBitOffset, SerializedBlobBitCount);
}

}
