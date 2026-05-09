// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetObjectBlobHandler.cpp —— Huge object 状态 blob 的实现。
// 关键点：
//   - Header 仅写 16 bits ObjectCount（单个 huge blob 中最多 65535 个对象）。
//   - CreateNetObjectBlob：发送端把已量化字节封装为 Reliable blob。
//   - CreateNetBlob       ：接收端创建空 blob 等待 RawData 填充（由 Assembler 完成）。
//   - OnNetBlobReceived   ：兜底报错，正常路径不会走到这里。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetObjectBlobHandler.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectBlobHandler)

namespace UE::Net::Private
{

// 直接基类构造：底层是 RawDataNetBlob，不使用 ReplicationStateDescriptor。
FNetObjectBlob::FNetObjectBlob(const UE::Net::FNetBlobCreationInfo& CreationInfo)
: FRawDataNetBlob(CreationInfo)
{
}

// Header 写入：固定 16 bits 的 ObjectCount → 上限 65535 个对象。
void FNetObjectBlob::SerializeHeader(FNetSerializationContext& Context, const FHeader& Header)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Header.ObjectCount, 16U);
}

void FNetObjectBlob::DeserializeHeader(FNetSerializationContext& Context, FHeader& OutHeader)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	OutHeader.ObjectCount = Reader->ReadBits(16U);
}

}

UNetObjectBlobHandler::UNetObjectBlobHandler()
: UNetBlobHandler()
{
}

UNetObjectBlobHandler::~UNetObjectBlobHandler()
{
}

// 发送端：把已经量化好的 raw bits 包成 FNetObjectBlob。
//   - typeId 取自本 handler 注册时分配的 NetBlobType。
//   - flags 强制 Reliable：huge object 状态必须可靠送达，否则会丢失 baseline。
TRefCountPtr<UE::Net::Private::FNetObjectBlob> UNetObjectBlobHandler::CreateNetObjectBlob(const TArrayView<const uint32> RawData, uint32 RawDataBitCount) const
{
	using namespace UE::Net;

	FNetBlobCreationInfo CreationInfo;
	CreationInfo.Type = GetNetBlobType();
	CreationInfo.Flags = ENetBlobFlags::Reliable;
	FNetObjectBlob* Blob = new FNetObjectBlob(CreationInfo);
	Blob->SetRawData(RawData, RawDataBitCount);
	return Blob;
}

// 接收端：创建空 blob，由调用方（Assembler）后续填充 RawData。
TRefCountPtr<UE::Net::FNetBlob> UNetObjectBlobHandler::CreateNetBlob(const FNetBlobCreationInfo& CreationInfo) const
{
	FNetObjectBlob* Blob = new FNetObjectBlob(CreationInfo);
	return Blob;
}

// 防御性实现：HugeObject 的接收路径由 ReplicationReader 直接处理 RawData 与 dequantize，
// 不会经由 NetBlobHandlerManager 派发到这里。一旦走到此处说明协议被破坏。
void UNetObjectBlobHandler::OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>&)
{
	ensureMsgf(false, TEXT("%s"), TEXT("NetObjectBlobHandler expects the blobs to be assembled and deserialized using a special path. This function is not expected to be called."));
	Context.SetError(UE::Net::GNetError_UnsupportedNetBlob);
}
