// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PartialNetObjectAttachmentHandler.h —— 大附件分片 handler
// -----------------------------------------------------------------------------
// 派生自 USequentialPartialNetBlobHandler。在通用"按序分片"基础上额外提供：
//   * 按可靠 / 不可靠（client / server 两套阈值）配置不同的"超过此位数才分片"门限；
//   * PreSerializeAndSplitNetBlob：先尝试一次序列化到固定缓冲，未溢出 → 直接构
//     造 ShrinkWrap 单条发出（不需要分片，避免量化开销）；溢出 → 转走父类
//     SplitNetBlob 分片路径。
//   * SplitRawDataNetBlob：把已经是 RawData 的 blob 在 BitCountSplitThreshold 之
//     上做按序分片；未达阈值则直接通过。
// 配套：UPartialNetObjectAttachmentHandlerConfig（继承自父类 Config）声明三个阈值。
// =============================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/SequentialPartialNetBlobHandler.h"
#include "Iris/ReplicationSystem/NetBlob/RawDataNetBlob.h"
#include "Iris/ReplicationSystem/NetExports.h"
#include "PartialNetObjectAttachmentHandler.generated.h"

UCLASS()
class UPartialNetObjectAttachmentHandlerConfig : public USequentialPartialNetBlobHandlerConfig
{
	GENERATED_BODY()

public:
	uint32 GetBitCountSplitThreshold() const { return BitCountSplitThreshold; }
	uint32 GetClientUnreliableBitCountSplitThreshold() const { return ClientUnreliableBitCountSplitThreshold; }
	uint32 GetServerUnreliableBitCountSplitThreshold() const { return ServerUnreliableBitCountSplitThreshold; }

private:
	/** How many bits a payload should have to recommend a split. Should be higher than MaxPartBitCount as splitting adds overhead. */
	// 中文：可靠路径门限（默认 192*8 bit ≈ 192 字节）。低于此值不分片，避免开销。
	UPROPERTY(Config)
	uint32 BitCountSplitThreshold = (128 + 64)*8;

	/** How many bits a unreliable payload should have to recommend a split on the client. Should be higher than MaxPartBitCount as splitting adds overhead. */
	// 中文：客户端 unreliable 门限（默认 850 字节）。客户端上行带宽紧张，门限较高
	// 让其优先发送整体而非分片，避免不可靠包丢失部分难以重组。
	UPROPERTY(Config)
	uint32 ClientUnreliableBitCountSplitThreshold = (850)*8;

	/** How many bits a unreliable payload should have to recommend a split on the server. Should be higher than MaxPartBitCount as splitting adds overhead. */
	// 中文：服务端 unreliable 门限（默认 256 字节）。服务端发往各客户端，门限低些
	// 利于命中 MTU 减少分片大小。
	UPROPERTY(Config)
	uint32 ServerUnreliableBitCountSplitThreshold = (256)*8;
};

struct FPartialNetObjectAttachmentHandlerInitParams
{
	UReplicationSystem* ReplicationSystem;
	const UPartialNetObjectAttachmentHandlerConfig* Config;
};

/**
 * NetBlobHandler that can split and assemble very large NetObjectAttachments.
 *
 * 中文：可拆分/重组超大 NetObjectAttachment 的 handler。继承自 SequentialPartialNet
 * BlobHandler，扩展两点：
 *   1) PreSerializeAndSplitNetBlob —— 先做"试序列化"，命中阈值则只发 ShrinkWrap
 *      （免重复量化）；溢出再走父类完整分片流程。
 *   2) SplitRawDataNetBlob —— RawData 的快速分片路径（已是位流，无量化开销）。
 */
UCLASS(transient, MinimalAPI)
class UPartialNetObjectAttachmentHandler final : public USequentialPartialNetBlobHandler
{
	GENERATED_BODY()

public:
	UPartialNetObjectAttachmentHandler();
	virtual ~UPartialNetObjectAttachmentHandler();

	void Init(const FPartialNetObjectAttachmentHandlerInitParams& InitParams);

	/** Serializes the NetBlob and either store the serialized version in a new NetBlob or splits into multiple partial NetBlobs. */
	// 中文：先把 attachment 序列化到固定大小（BitCountSplitThreshold）缓冲：
	//   * 未溢出 → 用 FShrinkWrapNetObjectAttachment 包成单条发出（无重量化）；
	//   * 溢出   → 转父类 SplitNetBlob 走完整分片协议（会重新执行序列化）。
	bool PreSerializeAndSplitNetBlob(uint32 ConnectionId, const TRefCountPtr<UE::Net::FNetObjectAttachment>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, bool bSerializeWithObject);

	/** Splits a RawDataNetBlob. The blob must have been created by a registered NetBlobHandler in order to be reconstructed on the receiving side. */
	// 中文：把已经是 RawData 的 blob 直接按位流切片。前提：原始 blob 的 type 必须
	// 已注册到 NetBlobHandlerManager，接收侧重组后才能正确还原对应 handler。
	bool SplitRawDataNetBlob(const TRefCountPtr<UE::Net::FRawDataNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName) const;

	const UPartialNetObjectAttachmentHandlerConfig* GetConfig() const;
private:
	// 中文：NetExports 用于 PreSerializeAndSplitNetBlob 中的"试序列化"——必须有
	// 自己的 ExportContext，避免污染上层连接的 export 状态。
	UE::Net::Private::FNetExports NetExports;
};

inline const UPartialNetObjectAttachmentHandlerConfig* UPartialNetObjectAttachmentHandler::GetConfig() const
{
	return CastChecked<const UPartialNetObjectAttachmentHandlerConfig>(Super::GetConfig(), ECastCheckedType::NullAllowed);
}
