// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SequentialPartialNetBlobHandler.h —— "按序分片"NetBlob handler 抽象基类
// -----------------------------------------------------------------------------
// 角色：通用的"无条件按序分片"handler。提供：
//   * SplitNetBlob：把任意 FNetBlob 切成一组 FPartialNetBlob（带 sequence number）；
//     接收侧 FNetBlobAssembler 按序号重组完整 blob。
//   * Init：保存 ReplicationSystem 和 Config（MaxPartBitCount / MaxPartCount）。
//   * Config 中 MaxPartBitCount × MaxPartCount = 单个 blob 的总位数上限（默认
//     128*8 × 4096 ≈ 512KB）。
// 子类：
//   * UPartialNetObjectAttachmentHandler —— 大附件分片，扩展"低于阈值不分片"逻辑。
// 强制契约：
//   * OnNetBlobReceived 不应被调用（子类不实现 → ensure 触发）；接收侧应通过
//     FNetBlobAssembler 重组后再分发到原 handler。
// 与 NetBlobAssembler 的协作：
//   FPartialNetBlob 自带 sequence/total/originalCreationInfo；Assembler 按序号
//   收齐后调用 GetAssembledNetBlob 还原 RawDataNetBlob，再走原 typeId 的 handler。
// =============================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandler.h"
#include "SequentialPartialNetBlobHandler.generated.h"

class FNetBlobHandlerManager;
class UReplicationSystem;
class USequentialPartialNetBlobHandlerConfig;
namespace UE::Net
{
	class FNetObjectReference;
}

struct FSequentialPartialNetBlobHandlerInitParams
{
	UReplicationSystem* ReplicationSystem;

	const USequentialPartialNetBlobHandlerConfig* Config;
};

UCLASS(Config=Engine, MinimalAPI)
class USequentialPartialNetBlobHandlerConfig : public UObject
{
	GENERATED_BODY()

public:
	uint32 GetMaxPartBitCount() const { return MaxPartBitCount; }
	uint32 GetMaxPartCount() const { return MaxPartCount; }
	// 中文：单个 blob 的总位数上限 = MaxPartBitCount × MaxPartCount。
	uint64 GetTotalMaxPayloadBitCount() const { return GetMaxPartBitCount()*uint64(GetMaxPartCount()); }

protected:
	/** How many bits a PartialNetBlob payload can hold at most. Cannot exceed 65535, but anything near the max packet size is discouraged as it is unlikely to fit. Keep it a power of two. */
	// 中文：每片最大位数（默认 128*8 = 1024 bit = 128 字节）。建议保持 2 的幂，
	// 接近 MTU 大小不推荐——单包难放，分片协议 header 后剩余空间不够整片。
	UPROPERTY(Config)
	uint32 MaxPartBitCount = 128*8;

	/** How many parts a NetBlob can be split into at most. If more parts are required the splitting will fail. Cannot exceed 65535. */
	// 中文：单 blob 最大片数（默认 4096）。超过则 SplitNetBlob 失败——blob 太大。
	UPROPERTY(Config)
	uint32 MaxPartCount = 4096;
};

UCLASS(abstract, MinimalApi, transient)
class USequentialPartialNetBlobHandler : public UNetBlobHandler
{
	GENERATED_BODY()

public:
	/** Unconditionally splits a NetBlob into a sequence of PartialNetBlobs which are small in size. Calls FNetBlob::Serialize(). */
	// 中文：无条件分片（无对象上下文版本）。沿用 blob 的 Ordered/Reliable 标志位。
	IRISCORE_API bool SplitNetBlob(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName = nullptr) const;

	/** Unconditionally splits a NetBlob into a sequence of PartialNetBlobs which are small in size. Calls FNetBlob::SerializeWithObject(). */
	// 中文：带对象上下文版本——序列化时调用 SerializeWithObject(RefHandle)，可省
	// 略 source object 引用编码。
	IRISCORE_API bool SplitNetBlob(UE::Net::FNetSerializationContext& Context, const UE::Net::FNetObjectReference& NetObjectReference, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs, const UE::Net::FNetDebugName* InDebugName = nullptr) const;

protected:
	IRISCORE_API USequentialPartialNetBlobHandler();

	IRISCORE_API void Init(const FSequentialPartialNetBlobHandlerInitParams& InitParams);

	const USequentialPartialNetBlobHandlerConfig* GetConfig() const { return Config; }

	// Convenience
	UReplicationSystem* ReplicationSystem;

private:
#if WITH_AUTOMATION_WORKER
	friend class UMockSequentialPartialNetBlobHandler;
#endif

	// UNetBlobHandler API. Not exposed to subclasses.
	// 中文：接收侧创建空 FPartialNetBlob 用于反序列化 → 由上层 FNetBlobAssembler
	// 按序号收齐后再 reconstruct 原 blob。
	virtual TRefCountPtr<FNetBlob> CreateNetBlob(const FNetBlobCreationInfo&) const override;

	/* A call will result in an error. Either override or use an external NetBlobAssembler instead and forward the final assembled blob to the appropriate handler. */
	// 中文：本类 OnNetBlobReceived 不应被直接调用 —— 上层应使用 FNetBlobAssembler
	// 重组后再转发到原 typeId 的 handler。直接调用会 ensure + SetError。
	virtual void OnNetBlobReceived(UE::Net::FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& NetBlob) override;

	const USequentialPartialNetBlobHandlerConfig* Config;
};
