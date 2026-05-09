// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RawDataNetBlob.h —— 原始字节流 NetBlob（无状态、不二次量化）
// -----------------------------------------------------------------------------
// 角色：FNetBlob 的派生类，仅承载"已经序列化好的位流"作为不透明 payload。
// 用途：
//   1) 上层（例如旧网络栈兼容路径或自定义 handler）已经把对象状态/RPC 参数序列
//      化为 bitstream，不希望 NetBlob 框架再走一次量化/反量化；
//   2) 与分片框架（FPartialNetBlob / FNetBlobAssembler / SequentialPartialNet
//      BlobHandler）协作时，"分片→重组"过程中可走优化的位流拷贝路径，避免重做
//      Quantize；
//   3) 适用于 Huge Object 数据流（从 FNetObjectAttachmentSendQueue 的 Huge 队列
//      流出后，内容已经是 quantize 过的 bit stream）。
// 关键约束：
//   - 序列化只是把 RawData[] 中的 RawDataBitCount 位写出/读回；不携带任何对象
//     引用或 NetToken（如需 export，使用 ShrinkWrapNetBlob）。
//   - SerializeWithObject / Serialize 实现完全相同——RefHandle 上下文不影响内容。
//   - 标记为 final：派生类只能扩展元信息，不能改写序列化语义。
// 与 ShrinkWrapNetBlob 区别：
//   - RawData 没有 OriginalBlob 引用，纯字节流；ShrinkWrap 还保留原 blob 句柄
//     用于 trace 和 export 反查。
// =============================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Containers/ArrayView.h"

namespace UE::Net
{

/**
 * Helper class for stateless data, such as when arbitrary data has been serialized to a bitstream.
 * The serialization will simply serialize the raw data regardless of whether a NetRefHandle is provided or not.
 * Things like splitting and assembling have optimized code paths for this type of blob.
 * You can inherit from this blob type but you cannot override the serialization functions.
 * @note Sending huge blobs that require splitting and assembling is strongly discouraged. 
 *
 * 中文：无状态原始字节流 blob 辅助类。无论是否给定 NetRefHandle，序列化都只输出
 * 原始 bit 数据。分片/重组对该 blob 类型有优化路径。可继承但不能覆写序列化方法。
 * 注意：发送需要分片重组的"巨型" blob 极不推荐——会显著增加抖动与丢包代价。
 */
class FRawDataNetBlob : public FNetBlob
{
public:
	IRISCORE_API FRawDataNetBlob(const FNetBlobCreationInfo&);

	/**
	 * Set the raw data via moving an array.
	 * @param RawData The array to be moved.
	 * @param RawDataBitCount The number of bits that should be serialized.
	 *        If RawDataBitCount is not a multiple of 32 then the (RawDataBitCount % 32)
	 *        least significant bits of the last uint32 are serialized.
	 *
	 * 中文：以移动语义设置原始位流。RawDataBitCount 不必是 32 的倍数；尾 uint32
	 * 的低 (RawDataBitCount % 32) 位才有效，高位将被忽略。
	 */
	IRISCORE_API void SetRawData(TArray<uint32>&& RawData, uint32 RawDataBitCount);

	/**
	 * Set the raw data. The data is copied.
	 * @param RawData The data to be copied.
	 * @param RawDataBitCount The number of bits that should be serialized.
	 *        If RawDataBitCount is not a multiple of 32 then the (RawDataBitCount % 32)
	 *        least significant bits of the last uint32 are serialized.
	 *
	 * 中文：以拷贝方式设置原始位流。语义同上。
	 */
	IRISCORE_API void SetRawData(const TArrayView<const uint32> RawData, uint32 RawDataBitCount);

	/** Returns the raw data. 中文：返回底层 uint32 数组（仅观察，不含尾位语义）。*/
	TArrayView<const uint32> GetRawData() const { return MakeArrayView(RawData.GetData(), RawData.Num()); }

	/** Returns the number of valid bits in the raw data. 中文：实际有效 bit 数。*/
	uint32 GetRawDataBitCount() const { return RawDataBitCount; }
	
protected:
	/** Serializes the raw data. 中文：写 RawDataBitCount（PackedUint32）+ RawDataBitCount 位的 bit stream。*/
	IRISCORE_API void InternalSerialize(FNetSerializationContext& Context) const;

	/** Deserializes the raw data. 中文：读 PackedUint32 长度后按位拷回 RawData。*/
	IRISCORE_API void InternalDeserialize(FNetSerializationContext& Context);

private:
	// 中文：四个 Serialize/Deserialize 重载全部 final，且最终都只走 InternalSerialize/Deserialize。
	//       与 NetRefHandle 无关——RawData 不依赖目标对象上下文。
	virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const override final;
	virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) override final;

	virtual void Serialize(FNetSerializationContext& Context) const override final;
	virtual void Deserialize(FNetSerializationContext& Context) override final;

	TArray<uint32> RawData;       // 中文：以 32-bit 字为单位的位流缓冲。
	uint32 RawDataBitCount;       // 中文：实际有效 bit 数（可不是 32 的倍数）。
};

}
