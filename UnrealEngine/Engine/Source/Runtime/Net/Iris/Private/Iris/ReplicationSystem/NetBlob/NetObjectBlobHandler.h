// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetObjectBlobHandler.h —— "Huge object" 巨型对象状态专用 handler。
//
// 用途：
//   当对象状态量（quantize 后的字节数）大到无法在一个 packet 中放下时，
//   FReplicationWriter 会把整段量化字节封装成一个 FNetObjectBlob（FRawDataNetBlob 派生），
//   再交给 PartialNetObjectAttachmentHandler / SequentialPartialNetBlobHandler 切片发送。
//
// 与普通 attachment 区别：
//   - 仅承载已序列化好的原始字节（RawData），不参与 ReplicationStateDescriptor 流程。
//   - OnNetBlobReceived 不会被直接调用：HugeObject 的接收路径由 ReplicationReader 与
//     专门的 Assembler 走完，最后 dequantize+apply。这里 OnNetBlobReceived 故意置为
//     ensureMsgf(false)，作为防御性兜底。
// =====================================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"
#include "Iris/ReplicationSystem/NetBlob/RawDataNetBlob.h"
#include "NetObjectBlobHandler.generated.h"

namespace UE::Net::Private
{

// 巨型对象状态 blob：在 RawData 之外加一段 Header，记录 batch 内的对象个数，
// 让接收端可以正确切分多个对象的状态。
class FNetObjectBlob : public FRawDataNetBlob
{
public:
	struct FHeader
	{
		// 本 blob 内打包了多少个对象的状态（最多 16 bits）。
		uint32 ObjectCount;
	};

	FNetObjectBlob(const UE::Net::FNetBlobCreationInfo&);
	
	// 写入 16 bits ObjectCount。
	static void SerializeHeader(FNetSerializationContext& Context, const FHeader& Header);
	// 读出 16 bits ObjectCount。
	static void DeserializeHeader(FNetSerializationContext& Context, FHeader& OutHeader);
};

}

/**
 * NetBlobHandler used for huge replicated objects. This blob will be split into PartialNetBlobs.
 */
// 巨型对象状态 handler：blob 总会以 Reliable 方式存在，并且在发送前由
// PartialNetObjectAttachmentHandler 切成多片 FPartialNetBlob。
UCLASS(transient, MinimalAPI)
class UNetObjectBlobHandler final : public UNetBlobHandler
{
	GENERATED_BODY()

	using FNetObjectBlob = UE::Net::Private::FNetObjectBlob;

public:
	UNetObjectBlobHandler();
	virtual ~UNetObjectBlobHandler();

	// 发送端入口：传入一段已经量化好的 raw bits 与位数 → 封装为 FNetObjectBlob。
	// 由 FReplicationWriter 在打包大对象时调用。
	TRefCountPtr<FNetObjectBlob> CreateNetObjectBlob(const TArrayView<const uint32> RawData, uint32 RawDataBitCount) const;

private:
	// 接收端反序列化触发：创建空的 FNetObjectBlob 等待填充 raw data。
	virtual TRefCountPtr<FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const override;
	// 防御性兜底：HugeObject 的实际接收处理由 ReplicationReader 走 special 路径，
	// 一旦走到这里说明协议异常。
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>&) override;
};
