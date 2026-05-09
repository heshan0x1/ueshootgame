// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ShrinkWrapNetBlob.h —— "塑封"已序列化数据的 NetBlob（避免二次量化）
// -----------------------------------------------------------------------------
// 解决的问题：
//   一个 RPC / Attachment 经常需要发往同一帧里的"多个目标连接"。如果对每个连接
//   都重新走一遍 Serialize（其中往往含有 Quantize / Token 导出 / Reference 收
//   集等耗时步骤），CPU 浪费严重。
// 解决方案：
//   发送侧只 Serialize 一次到临时 buffer，然后用本类把 buffer 包起来作为同一份
//   "塑封"blob 派发给多条连接的发送队列。本类的 Serialize 仅仅把 buffer 里已有
//   的位流原样写入数据流（与 RawDataNetBlob 类似，但保留对原 blob 的引用以支
//   持 trace 与导出反查）。
// 类型：
//   - FShrinkWrapNetBlob          —— 塑封普通 FNetBlob（无 target object 引用）
//   - FShrinkWrapNetObjectAttachment —— 塑封 FNetObjectAttachment（含 target ref，
//     在 Serialize 时还要把 target/source NetObjectReference 加入 pending exports）
// 重要约束：
//   - 仅用于"发送端"。接收端永远走原 blob 类型的 DeserializeWithObject /
//     Deserialize；本类的反序列化方法被 checkf 强制不可调用。
//   - 当启用 trace 时，会回退到 OriginalBlob 的 Serialize 路径（成本变高，但
//     trace 维度更精细）。
// 与 PartialNetBlob、PartialNetObjectAttachmentHandler 的协作：
//   PartialNetObjectAttachmentHandler::PreSerializeAndSplitNetBlob 会先尝试把
//   blob 序列化到 buffer，如果未溢出就构造一个 ShrinkWrap 实例发出（避免重复量
//   化）；溢出则回退到 PartialNetBlob 分片路径。
// =============================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Net/Core/NetToken/NetToken.h"

namespace UE::Net
{

/**
 * A ShrinkWrapNetBlob/NetObjectAttachment is typically used on the sending side for data with multiple destinations.
 * In that case the contents of the original blob can be serialized, once, to a buffer and then wrapped
 * in an instance of this class. The serialization of the buffer is likely faster than the original
 * serialization as no particular logic needs to be performed and serializing a buffer is an optimized path.
 * @note If tracing is enabled the OriginalBlob will be serialized instead of the already serialized buffer.
 *       This is for debugging purposes.
 * @note Deserialization will always be performed by the original blob type.
 *
 * 中文：发送侧"一次序列化、多次塑封发送"优化的载体类。当一份数据要分发给多条
 * 连接时，把原 blob 序列化到 buffer 一次，再包成 ShrinkWrap 后续直接拷贝即可。
 * trace 启用时会回退到原 blob 序列化路径以便记录细节。接收侧永远走原 blob 类型。
 */
class FShrinkWrapNetBlob final : public FNetBlob
{
public:
	// 中文：构造时 NetTokenExportsArray 直接快照当前 batch 的 pending NetToken 列表，
	//       因为序列化已经发生（pending exports 已被原 blob 触发）。
	IRISCORE_API FShrinkWrapNetBlob(FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& OriginalBlob, TArray<uint32>&& Payload, uint32 PayloadBitCount);

private:
	// 中文：对象引用 export 转发到原 blob（保证导出语义一致）。
	virtual TArrayView<const FNetObjectReference> GetNetObjectReferenceExports() const override final;
	// 中文：NetToken export 用本地快照副本（已在构造期采集）。
	virtual TArrayView<const FNetToken> GetNetTokenExports() const override final;
	// 中文：trace 模式 → 走原 blob；正常模式 → InternalSerialize 直拷字节流。
	virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const override final;
	// 中文：禁止调用 —— 接收端走原 blob。
	virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) override final;

	virtual void Serialize(FNetSerializationContext& Context) const override final;
	virtual void Deserialize(FNetSerializationContext& Context) override final;

	// 中文：把缓存好的 SerializedBlob 按 SerializedBlobBitCount 位写出。
	void InternalSerialize(FNetSerializationContext& Context) const;

	TRefCountPtr<FNetBlob> OriginalBlob;                            // 原 blob，trace + reference exports 反查
	TArray<FNetToken, TInlineAllocator<4>> NetTokenExportsArray;    // 构造时快照的待导出 NetToken
	TArray<uint32> SerializedBlob;                                  // 已序列化好的位流缓冲
	uint32 SerializedBlobBitCount;                                  // 有效位数（不必是 32 倍数）
};

// 中文：对象附件版本 —— 多了一步：在非 trace 路径里要把 target/source object
// 引用主动加入 pending exports（普通 ShrinkWrap 没有 target ref 概念）。
class FShrinkWrapNetObjectAttachment final : public FNetBlob
{
public:
	IRISCORE_API FShrinkWrapNetObjectAttachment(FNetSerializationContext& Context, const TRefCountPtr<FNetObjectAttachment>& OriginalBlob, TArray<uint32>&& Payload, uint32 PayloadBitCount);

private:
	virtual TArrayView<const FNetObjectReference> GetNetObjectReferenceExports() const override final;
	virtual TArrayView<const FNetToken> GetNetTokenExports() const override final;
	virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const override final;
	virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) override final;

	virtual void Serialize(FNetSerializationContext& Context) const override final;
	virtual void Deserialize(FNetSerializationContext& Context) override final;

	void InternalSerialize(FNetSerializationContext& Context) const;

	TRefCountPtr<FNetObjectAttachment> OriginalBlob;                // 必为 attachment 派生类型，含 target/source object ref
	TArray<FNetToken, TInlineAllocator<4>> NetTokenExportsArray;
	TArray<uint32> SerializedBlob;
	uint32 SerializedBlobBitCount;
};

}
