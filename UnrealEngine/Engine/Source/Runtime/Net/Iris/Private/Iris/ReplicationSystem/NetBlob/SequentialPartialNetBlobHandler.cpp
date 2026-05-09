// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SequentialPartialNetBlobHandler.cpp —— "按序分片"NetBlob handler 实现
// -----------------------------------------------------------------------------
// 仅是个薄壳：实际切片在 FPartialNetBlob::SplitNetBlob 中完成（见 PartialNetBlob.cpp）。
// 本类负责：
//   * 拼装 FPartialNetBlob::FSplitParams（MaxPartBitCount/MaxPartCount/DebugName）；
//   * 把 blob 自身的 Ordered/Reliable 标志位传递到 PartialNetBlob 的 CreationInfo
//     —— 这样接收侧重组的 sequence 能保留原语义；
//   * 接收侧 CreateNetBlob 仅 new 空 PartialNetBlob，等 Assembler 重组。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/SequentialPartialNetBlobHandler.h"
#include "Iris/ReplicationSystem/NetBlob/PartialNetBlob.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializationContext.h"


// 

#include UE_INLINE_GENERATED_CPP_BY_NAME(SequentialPartialNetBlobHandler)
USequentialPartialNetBlobHandler::USequentialPartialNetBlobHandler()
: ReplicationSystem(nullptr)
, Config(nullptr)
{
}

void USequentialPartialNetBlobHandler::Init(const FSequentialPartialNetBlobHandlerInitParams& InitParams)
{
	ReplicationSystem = InitParams.ReplicationSystem;
	Config = InitParams.Config;
}

bool USequentialPartialNetBlobHandler::SplitNetBlob(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName) const
{
	// 中文：分片入口（无对象上下文）。
	using namespace UE::Net;

	FNetBlobCreationInfo CreationInfo = {};
	CreationInfo.Type = GetNetBlobType();
	// 中文：仅传递 Ordered/Reliable —— 其它标志（HasExports 等）不应继承到 partial。
	CreationInfo.Flags = Blob->GetCreationInfo().Flags & (ENetBlobFlags::Ordered | ENetBlobFlags::Reliable);

	FPartialNetBlob::FSplitParams SplitParams = {};
	SplitParams.MaxPartBitCount = Config->GetMaxPartBitCount();
	SplitParams.MaxPartCount = Config->GetMaxPartCount();
	if (InDebugName == nullptr)
	{
		// 中文：未显式指定 DebugName，从 Blob 的 ReplicationStateDescriptor 取——
		// 便于 trace 与日志定位。
		if (const FReplicationStateDescriptor* Descriptor = Blob->GetReplicationStateDescriptor())
		{
			InDebugName = Descriptor->DebugName;
		}
	}
	if (InDebugName != nullptr)
	{
		SplitParams.DebugName = *InDebugName;
	}

	return FPartialNetBlob::SplitNetBlob(Context, CreationInfo, SplitParams, Blob, OutPartialBlobs);
}

bool USequentialPartialNetBlobHandler::SplitNetBlob(UE::Net::FNetSerializationContext& Context, const UE::Net::FNetObjectReference& NetObjectReference, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName) const
{
	// 中文：分片入口（带对象上下文）—— FPartialNetBlob 在序列化每片时调用
	// SerializeWithObject(RefHandle)，可省略 source object 引用编码。
	using namespace UE::Net;

	FNetBlobCreationInfo CreationInfo = {};
	CreationInfo.Type = GetNetBlobType();
	CreationInfo.Flags = Blob->GetCreationInfo().Flags & (ENetBlobFlags::Ordered | ENetBlobFlags::Reliable);

	FPartialNetBlob::FSplitParams SplitParams = {};
	SplitParams.MaxPartBitCount = Config->GetMaxPartBitCount();
	SplitParams.MaxPartCount = Config->GetMaxPartCount();
	SplitParams.NetObjectReference = NetObjectReference;
	SplitParams.bSerializeWithObject = true;
	if (InDebugName == nullptr)
	{
		if (const FReplicationStateDescriptor* Descriptor = Blob->GetReplicationStateDescriptor())
		{
			InDebugName = Descriptor->DebugName;
		}
	}
	if (InDebugName != nullptr)
	{
		SplitParams.DebugName = *InDebugName;
	}

	return FPartialNetBlob::SplitNetBlob(Context, CreationInfo, SplitParams, Blob, OutPartialBlobs);
}

TRefCountPtr<UE::Net::FNetBlob> USequentialPartialNetBlobHandler::CreateNetBlob(const FNetBlobCreationInfo& CreationInfo) const
{
	// 中文：接收侧——框架按 typeId 找到本 handler，调用此函数 new 空 PartialNetBlob，
	// 由上层 FNetBlobAssembler 控制反序列化与重组流程。
	using namespace UE::Net;

	FPartialNetBlob* Blob = new FPartialNetBlob(CreationInfo);
	return Blob;
}

void USequentialPartialNetBlobHandler::OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& NetBlob)
{
	// 中文：本类 OnNetBlobReceived 永远不应被直接调用——上层应通过 FNetBlobAssembler
	// 把所有 partial 收齐重组成原 blob，再转发到对应 typeId 的 handler。
	// 直接调用即是协议错误，置 ensure 并 SetError 让上层关闭连接。
	ensureMsgf(false, TEXT("%s"), TEXT("SequentialPartialNetBlobHandler expects the blobs to be assembled via FNetBlobAssembler and then further processed, e.g. via the original blob type's NetBlobHandler. This function is not expected to be called."));
	Context.SetError(UE::Net::GNetError_UnsupportedNetBlob);
}
